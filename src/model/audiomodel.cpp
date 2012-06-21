#include "audiomodel.hpp"
#include "loaderthread.hpp"
#include "transport.hpp"
#include "defines.hpp"

#include <QMutexLocker>
#include <vector>
#include <QMetaObject>
#include <QTimer>

using namespace dj;
using namespace dj::audio;

#include <iostream>
using std::cerr;
using std::endl;

namespace {
   const int audible_timeout_ms = 200;
}

template <typename T>
T clamp(T val, T bottom, T top) {
   if (val < bottom)
      return bottom;
   if (val > top)
      return top;
   return val;
}

class AudioModel::PlayerSetBuffersCommand : public dj::audio::PlayerCommand {
   public:
      PlayerSetBuffersCommand(unsigned int idx,
            AudioModel * model,
            AudioBuffer * audio_buffer,
            BeatBuffer * beat_buffer
            ) :
         dj::audio::PlayerCommand(idx),
         mAudioModel(model),
         mAudioBuffer(audio_buffer),
         mBeatBuffer(beat_buffer),
         mOldAudioBuffer(NULL),
         mOldBeatBuffer(NULL) { }
      virtual ~PlayerSetBuffersCommand() { }

      virtual void execute() {
         Player * p = player(); 
         if(p != NULL){
            mOldBeatBuffer = p->beat_buffer();
            mOldAudioBuffer = p->audio_buffer();
            p->audio_buffer(mAudioBuffer);
            p->beat_buffer(mBeatBuffer);
         }
      }
      virtual void execute_done() {
         //remove a copy of the old buffers from the list
         if (mOldAudioBuffer) {
            AudioBufferPtr buffer(mOldAudioBuffer);
            mAudioModel->mPlayingAudioFiles.removeOne(buffer);
         }
         if (mOldBeatBuffer) {
            BeatBufferPtr buffer(mOldBeatBuffer);
            mAudioModel->mPlayingAnnotationFiles.removeOne(buffer);
         }
         //execute the super class's done action
         PlayerCommand::execute_done();
      }
      virtual bool store(CommandIOData& /*data*/) const {
         //TODO
         return false;
      }
   private:
      AudioModel * mAudioModel;
      AudioBuffer * mAudioBuffer;
      BeatBuffer * mBeatBuffer;
      AudioBuffer * mOldAudioBuffer;
      BeatBuffer * mOldBeatBuffer;
};

class AudioModel::PlayerState {
   public:
      PlayerState() :
         mCurrentFrame(0),
         mNumFrames(0),
         mSpeed(0.0),
         mPostFreeSpeedUpdates(0),
         mMaxSampleValue(0.0) { }

      QHash<QString, int> mParamInt;
      QHash<QString, bool> mParamBool;
      QHash<QString, TimePoint> mParamPosition;

      //okay to update in audio thread
      unsigned int mCurrentFrame;
      unsigned int mNumFrames;
      double mSpeed;
      //we count the number of speed updates so that after going 'free' we get at least two updates to the gui
      unsigned int mPostFreeSpeedUpdates;
      float mMaxSampleValue;
};

//TODO how to get it to run at the end of the frame?
class AudioModel::QueryPlayState : public MasterCommand {
   private:
      AudioModel * mAudioModel;
   public:
      std::vector<AudioModel::PlayerState* > mStates;
      unsigned int mNumPlayers;
      float mMasterMaxVolume;
      TimePoint mMasterTransportPosition;

      QueryPlayState(AudioModel * model) : mAudioModel(model), mMasterMaxVolume(0.0) {
         mNumPlayers = mAudioModel->player_count();
         for(unsigned int i = 0; i < mNumPlayers; i++)
            mStates.push_back(new AudioModel::PlayerState);
         mStates.resize(mNumPlayers);
      }
      virtual ~QueryPlayState() {
         for(unsigned int i = 0; i < mNumPlayers; i++)
            delete mStates[i];
      }
      virtual void execute(){
         mMasterTransportPosition = master()->transport()->position();
         mMasterMaxVolume = master()->max_sample_value();
         master()->max_sample_value_reset();
         for(unsigned int i = 0; i < mNumPlayers; i++) {
            Player * player = master()->players()[i];
            mStates[i]->mCurrentFrame = player->frame();
            mStates[i]->mMaxSampleValue = player->max_sample_value();
            mStates[i]->mSpeed = player->play_speed();
            player->max_sample_value_reset();
         }
      }
      virtual void execute_done() {
         for(unsigned int i = 0; i < mNumPlayers; i++)
            mAudioModel->update_player_state(i, mStates[i]);

         int master_level = static_cast<int>(100.0 * mMasterMaxVolume);
         if (master_level > 0) {
            QMetaObject::invokeMethod(mAudioModel, "relay_master_audio_level", 
                  Qt::QueuedConnection,
                  Q_ARG(int, master_level));
         }

         QMetaObject::invokeMethod(mAudioModel, "relay_master_position", 
               Qt::QueuedConnection,
               Q_ARG(TimePoint, mMasterTransportPosition));
      }
      //this command shouldn't be stored
      virtual bool store(CommandIOData& /* data */) const { return false; }
};

class AudioModel::ConsumeThread : public QThread {
   private:
      Scheduler * mScheduler;
      AudioModel * mModel;
   public:
      ConsumeThread(AudioModel * model, Scheduler * scheduler) : mScheduler(scheduler), mModel(model) { }

      void run() {
         while(true) {
            AudioModel::QueryPlayState * cmd = new AudioModel::QueryPlayState(mModel);
            mScheduler->execute(cmd);
            mScheduler->execute_done_actions();
            //XXX if the UI becomes unresponsive, increase this value
            msleep(30);
         }
      }
};

AudioModel * AudioModel::cInstance = NULL;

AudioModel::AudioModel() :
   QObject(),
   mPlayerStates(),
   mPlayerStatesMutex(QMutex::Recursive),
   mMasterBPM(0.0),
   mCrossFadeEnabled(false),
   mCrossFadePosition(0),
   mPlayerAudibleThresholdVolume(0.05 * one_scale), //XXX make this configurable?
   mCrossfadeAudibleThresholdPosition(0.05 * one_scale)
{
   unsigned int num_players = 2;

   //register signal types
   qRegisterMetaType<TimePoint>("TimePoint");
   qRegisterMetaType<AudioBufferPtr>("AudioBufferPtr");
   qRegisterMetaType<BeatBufferPtr>("BeatBufferPtr");

   mCrossFadePlayers[0] = 0;
   mCrossFadePlayers[1] = 1;

   mAudioIO = dj::audio::AudioIO::instance();
   mMaster = dj::audio::Master::instance();

   mNumPlayers = num_players;
   for(unsigned int i = 0; i < mNumPlayers; i++) {
      mMaster->add_player();
      mPlayerStates.push_back(new PlayerState());
      LoaderThread * newThread = new LoaderThread;
      mThreadPool.push_back(newThread);
      QObject::connect(newThread, SIGNAL(load_progress(int, int)),
            SLOT(relay_audio_file_load_progress(int, int)),
            Qt::QueuedConnection);
      QObject::connect(newThread, SIGNAL(load_complete(int, AudioBufferPtr, BeatBufferPtr)),
            SLOT(relay_player_buffers_loaded(int, AudioBufferPtr, BeatBufferPtr)),
            Qt::QueuedConnection);
   }

   //set up the bool action mappings
   mPlayerStateActionMapping["mute"] = player_onoff_action_pair_t(PlayerStateCommand::MUTE, PlayerStateCommand::NO_MUTE);
   mPlayerStateActionMapping["sync"] = player_onoff_action_pair_t(PlayerStateCommand::SYNC, PlayerStateCommand::NO_SYNC);
   mPlayerStateActionMapping["loop"] = player_onoff_action_pair_t(PlayerStateCommand::LOOP, PlayerStateCommand::NO_LOOP);
   mPlayerStateActionMapping["cue"] = player_onoff_action_pair_t(PlayerStateCommand::OUT_CUE, PlayerStateCommand::OUT_MAIN);
   mPlayerStateActionMapping["pause"] = player_onoff_action_pair_t(PlayerStateCommand::PAUSE, PlayerStateCommand::PLAY);

   //set up the double action mappings
   mPlayerDoubleActionMapping["volume"] = PlayerDoubleCommand::VOLUME;
   mPlayerDoubleActionMapping["speed"] = PlayerDoubleCommand::PLAY_SPEED;

   //set up position mappings
   mPlayerPositionActionMapping["play"] = PlayerPositionCommand::PLAY;
   mPlayerPositionActionMapping["play_relative"] = PlayerPositionCommand::PLAY_RELATIVE;
   mPlayerPositionActionMapping["start"] = PlayerPositionCommand::START;
   mPlayerPositionActionMapping["end"] = PlayerPositionCommand::END;
   mPlayerPositionActionMapping["loop_start"] = PlayerPositionCommand::LOOP_START;
   mPlayerPositionActionMapping["loop_end"] = PlayerPositionCommand::LOOP_END;

   for(unsigned int i = 0; i < mNumPlayers; i++) {
      dj::audio::Player * player = mMaster->players()[i];
      player->sync(true);
      player->out_state(Player::MAIN_MIX);
      player->play_state(Player::PLAY);

      //init player states
      //bool
      mPlayerStates[i]->mParamBool["mute"] = player->muted();
      mPlayerStates[i]->mParamBool["sync"] = player->syncing();
      mPlayerStates[i]->mParamBool["loop"] = player->looping();
      mPlayerStates[i]->mParamBool["cue"] = (player->out_state() == dj::audio::Player::CUE);
      mPlayerStates[i]->mParamBool["pause"] = (player->play_state() == dj::audio::Player::PAUSE);
      mPlayerStates[i]->mParamBool["audible"] = false;

      //int
      mPlayerStates[i]->mParamInt["volume"] = one_scale * player->volume();
      mPlayerStates[i]->mParamInt["speed"] = one_scale + one_scale * player->play_speed(); //percent

      //position
      mPlayerStates[i]->mParamPosition["start"] = player->start_position();
      //XXX should we query these on each load?
      mPlayerStates[i]->mParamPosition["end"] = TimePoint(-1);
      mPlayerStates[i]->mParamPosition["loop_start"] = TimePoint(-1);
      mPlayerStates[i]->mParamPosition["loop_end"] = TimePoint(-1);
   }

   //hook up and start the consume thread
   mConsumeThread = new ConsumeThread(this, mMaster->scheduler());
   mConsumeThread->start();

   mAudibleTimer = new QTimer(this);
   mAudibleTimer->setInterval(audible_timeout_ms);
   QObject::connect(mAudibleTimer, SIGNAL(timeout()), SLOT(players_eval_audible()));
   mAudibleTimer->start();
}

AudioModel::~AudioModel() {
}

AudioModel * AudioModel::instance(){
   if (!cInstance)
      cInstance = new AudioModel();
   return cInstance;
}

unsigned int AudioModel::sample_rate() const { return mAudioIO->getSampleRate(); }
unsigned int AudioModel::player_count() const { return mNumPlayers; }

double AudioModel::master_bpm() const {
   return mMasterBPM;
}

void AudioModel::set_player_position(int player_index, const TimePoint &val, bool absolute){
   if (player_index < 0 || player_index >= (int)mNumPlayers)
      return;

   Command * cmd = NULL;
   if (absolute)
      cmd = new dj::audio::PlayerPositionCommand(
            player_index, PlayerPositionCommand::PLAY, val);
   else
      cmd = new dj::audio::PlayerPositionCommand(
            player_index, PlayerPositionCommand::PLAY_RELATIVE, val);
   queue_command(cmd);
}

void AudioModel::set_player_position_frame(int player_index, int frame, bool absolute) {
   if (player_index < 0 || player_index >= (int)mNumPlayers)
      return;

   if (absolute) {
      if (frame < 0)
         frame = 0;
   } else {
      //do nothing if we are doing a relative seek by 0
      if (frame == 0)
         return;
   }

   Command * cmd = NULL;
   if (absolute)
      cmd = new dj::audio::PlayerPositionCommand(
            player_index, PlayerPositionCommand::PLAY, frame);
   else
      cmd = new dj::audio::PlayerPositionCommand(
            player_index, PlayerPositionCommand::PLAY_RELATIVE, frame);
   queue_command(cmd);
}

void AudioModel::set_player_position_beat_relative(int player_index, int beats) {
   TimePoint point;
   unsigned int abs_beat = abs(beats);
   int bar = 0;
   while(abs_beat >= 4) {
      abs_beat -= 4;
      bar += 1;
   }
   point.at_bar(bar, abs_beat);
   if (beats < 0)
      point = -point;

   set_player_position(player_index, point, false);
}

void AudioModel::update_player_state(int player_index, PlayerState * new_state){
   if (player_index < 0 || player_index >= (int)mNumPlayers)
      return;
   QMutexLocker lock(&mPlayerStatesMutex);
   PlayerState * pstate = mPlayerStates[player_index];

   int frame = new_state->mCurrentFrame;
   if ((unsigned int)frame != pstate->mCurrentFrame) {
      pstate->mCurrentFrame = frame;
      //emit(relay_player_position_changed(player_index, frame));
      QMetaObject::invokeMethod(this, "relay_player_value", 
            Qt::QueuedConnection,
            Q_ARG(int, player_index),
            Q_ARG(QString, "update_frame"),
            Q_ARG(int, frame));
   }

   //only return rate info while syncing
   //TODO maybe send 2 values after starting to run free so that we are sure?
   if (pstate->mParamBool["sync"] || pstate->mPostFreeSpeedUpdates < 2) {
      pstate->mPostFreeSpeedUpdates += 1;
      int speed_percent = (new_state->mSpeed - 1.0) * one_scale;
      if (pstate->mParamInt["speed"] != speed_percent) {
         pstate->mParamInt["speed"] = speed_percent;
         QMetaObject::invokeMethod(this, "relay_player_value", 
               Qt::QueuedConnection,
               Q_ARG(int, player_index),
               Q_ARG(QString, "update_speed"),
               Q_ARG(int, speed_percent));
      }
   }

   int audio_level = static_cast<int>(new_state->mMaxSampleValue * 100.0);
   if (audio_level > 0) {
      QMetaObject::invokeMethod(this, "relay_player_value", 
            Qt::QueuedConnection,
            Q_ARG(int, player_index),
            Q_ARG(QString, "update_audio_level"),
            Q_ARG(int, audio_level));
   }
}

void AudioModel::set_player_eq(int player_index, int band, int value) {
   if (band < 0 || band > 2)
      return;

   int ione_scale = one_scale;

   if (value < -ione_scale)
      value = -ione_scale;
   else if (value > ione_scale)
      value = ione_scale;

   float remaped = 0.0;
   if (value > 0) {
      remaped = (6.0 * value) / (float)one_scale;
   } else {
      remaped = (70.0 * value) / (float)one_scale;
   }

   PlayerDoubleCommand::action_t action;
   QString name;
   switch(band) {
      case 0:
         action = PlayerDoubleCommand::EQ_LOW;
         name = "eq_low";
         break;
      case 1:
         action = PlayerDoubleCommand::EQ_MID;
         name = "eq_mid";
         break;
      case 2:
         action = PlayerDoubleCommand::EQ_HIGH;
         name = "eq_high";
         break;
   }

   queue_command(new PlayerDoubleCommand(player_index, action, remaped));
   emit(player_value_changed(player_index, name, value));
}

void AudioModel::relay_player_buffers_loaded(int player_index,
      AudioBufferPtr audio_buffer,
      BeatBufferPtr beat_buffer) {
   if (player_index < 0 || player_index >= (int)mNumPlayers)
      return;
   QMutexLocker lock(&mPlayerStatesMutex);
   if (audio_buffer)
      mPlayerStates[player_index]->mNumFrames = audio_buffer->length();
   else
      mPlayerStates[player_index]->mNumFrames = 0;

   mPlayingAudioFiles <<  audio_buffer;
   mPlayingAnnotationFiles << beat_buffer;
   queue_command(new PlayerSetBuffersCommand(player_index, this, audio_buffer.data(), beat_buffer.data()));
   player_trigger(player_index, "reset");
   emit(player_buffers_changed(player_index, audio_buffer, beat_buffer));
}

void AudioModel::relay_player_value(int player_index, QString name, int value){
   emit(player_value_changed(player_index, name, value));
}

void AudioModel::relay_master_audio_level(int percent) {
   emit(master_value_changed("update_audio_level", percent));
}

void AudioModel::relay_master_position(TimePoint position) {
   emit(master_value_changed("transport_position", position));
}

void AudioModel::relay_audio_file_load_progress(int player_index, int percent){
   emit(player_value_changed(player_index, "update_progress", percent));
}

void AudioModel::players_eval_audible() {
   for(unsigned int player_index = 0; player_index < mPlayerStates.size(); player_index++)
      player_eval_audible(player_index);
}

void AudioModel::master_set(QString name, bool value) {
   if (name == "crossfade") {
      if (value != mCrossFadeEnabled) {
         mCrossFadeEnabled = value;
         queue_command(new MasterBoolCommand(value ? MasterBoolCommand::XFADE : MasterBoolCommand::NO_XFADE));
      }
   } else
      cerr << DJ_FILEANDLINE << name.toStdString() << " is not a master_set (bool) arg" << endl;
}

void AudioModel::master_set(QString name, int value) {
   //TODO compare against last set value?
   
   if (name == "volume") {
      value = clamp(value, 0, (int)(1.5 * one_scale));
      queue_command(new MasterDoubleCommand(MasterDoubleCommand::MAIN_VOLUME, (double)value / (double)one_scale));
      emit(master_value_changed("volume", value));
   } else if (name == "cue_volume") {
      value = clamp(value, 0, (int)(1.5 * one_scale));
      queue_command(new MasterDoubleCommand(MasterDoubleCommand::CUE_VOLUME, (double)value / (double)one_scale));
      emit(master_value_changed("cue_volume", value));
   } else if (name == "crossfade_position") {
      value = clamp(value, 0, (int)one_scale);
      if (value != mCrossFadePosition) {
         mCrossFadePosition = value;
         queue_command(new MasterDoubleCommand(MasterDoubleCommand::XFADE_POSITION, (double)value / (double)one_scale));
         emit(master_value_changed("crossfade_position", value));
      }
   } else if (name == "sync_to_player") {
      if (value < 0 || value >= (int)mNumPlayers)
         return;

      QMutexLocker lock(&mPlayerStatesMutex);
      queue_command(new MasterIntCommand(MasterIntCommand::SYNC_TO_PLAYER, value));

      if(!mPlayerStates[value]->mParamBool["sync"]) {
         mPlayerStates[value]->mParamBool["sync"] = true;
         emit(player_value_changed(value, "sync", true));
      }
   } else
      cerr << name.toStdString() << " is not a master_set (int) arg" << endl;
}

void AudioModel::master_set(QString name, double value) {
   if (name == "bpm") {
      if (value != mMasterBPM) {
         mMasterBPM = value;
         queue_command(new TransportBPMCommand(mMaster->transport(), value));
         emit(master_value_changed(name, value));
      }
   }
}

void AudioModel::set_master_cross_fade_players(int left, int right){
   if (left < 0 || left >= (int)mNumPlayers)
      return;
   if (right < 0 || right >= (int)mNumPlayers)
      return;
   mCrossFadePlayers[0] = left;
   mCrossFadePlayers[1] = right;
   queue_command(new MasterXFadeSelectCommand((unsigned int)left, (unsigned int)right));
}

bool AudioModel::player_state_bool(int player_index, QString name) {
   if (player_index < 0 || player_index >= (int)mNumPlayers)
      return false;
   QMutexLocker lock(&mPlayerStatesMutex);
   PlayerState * pstate = mPlayerStates[player_index];

   QHash<QString, bool>::const_iterator itr = pstate->mParamBool.find(name);
   if (itr == pstate->mParamBool.end())
      return false;
   return *itr;
}

int AudioModel::player_state_int(int player_index, QString name) {
   if (player_index < 0 || player_index >= (int)mNumPlayers)
      return 0;
   QMutexLocker lock(&mPlayerStatesMutex);
   PlayerState * pstate = mPlayerStates[player_index];

   if (name == "frame") {
      return pstate->mCurrentFrame;
   }

   QHash<QString, int>::const_iterator itr = pstate->mParamInt.find(name);
   if (itr == pstate->mParamInt.end())
      return 0;
   return *itr;
}

void AudioModel::player_trigger(int player_index, QString name) {
   if (player_index < 0 || player_index >= (int)mNumPlayers)
      return;

   if (name == "reset")
      set_player_position_frame(player_index, 0);
   else if (name == "seek_forward")
      set_player_position_beat_relative(player_index, 1);
   else if (name == "seek_back")
      set_player_position_beat_relative(player_index, -1);
   else if (name == "clear")
      queue_command(new PlayerSetBuffersCommand(player_index, this, NULL, NULL));
   else if (name != "load") {
      QMutexLocker lock(&mPlayerStatesMutex);
      PlayerState * pstate = mPlayerStates[player_index];
      QHash<QString, bool>::iterator state_itr = pstate->mParamBool.find(name);
      if (state_itr == pstate->mParamBool.end()) {
         cerr << DJ_FILEANDLINE << name.toStdString() << " is not a valid player_trigger arg" << endl;
         return;
      }
      //toggle
      player_set(player_index, name, !*state_itr);
   }

   emit(player_triggered(player_index, name));
}

void AudioModel::player_set(int player_index, QString name, bool value) {
   if (player_index < 0 || player_index >= (int)mNumPlayers)
      return;
   QMutexLocker lock(&mPlayerStatesMutex);
   PlayerState * pstate = mPlayerStates[player_index];

   //special case
   if (name == "seeking") {
      //pause while seeking
      if (value) {
         if (!pstate->mParamBool["pause"])
            queue_command(new dj::audio::PlayerStateCommand(player_index, PlayerStateCommand::PAUSE));
      } else {
         if (!pstate->mParamBool["pause"])
            queue_command(new dj::audio::PlayerStateCommand(player_index, PlayerStateCommand::PLAY));
      }
      return;
   }

   //get the state for this name
   QHash<QString, bool>::iterator state_itr = pstate->mParamBool.find(name);
   if (state_itr == pstate->mParamBool.end()) {
      cerr << DJ_FILEANDLINE << name.toStdString() << " is not a valid player_set (bool) arg" << endl;
      return;
   }

   //return if there isn't anything to be done
   if (*state_itr == value)
      return;

   //get the actions
   QHash<QString, player_onoff_action_pair_t>::const_iterator action_itr = mPlayerStateActionMapping.find(name);
   if (action_itr == mPlayerStateActionMapping.end()) {
      cerr << DJ_FILEANDLINE << name.toStdString() << " is not a valid player_set (bool) arg [action not found]" << endl;
      return;
   }

   //set the new value
   *state_itr = value;

   //if we'return going free, set our post free speed update index
   if (name == "sync" && !value)
      pstate->mPostFreeSpeedUpdates = 0;


   //queue the actual command [who's action is stored in the action_itr]
   queue_command(new dj::audio::PlayerStateCommand(player_index, value ? action_itr->first : action_itr->second));
   emit(player_value_changed(player_index, name, value));
}

void AudioModel::player_set(int player_index, QString name, int value) {
   if (player_index < 0 || player_index >= (int)mNumPlayers)
      return;
   QMutexLocker lock(&mPlayerStatesMutex);
   PlayerState * pstate = mPlayerStates[player_index];

   if (name == "eq_low") {
      set_player_eq(player_index, 0, value);
   } else if (name == "eq_mid") {
      set_player_eq(player_index, 1, value);
   } else if (name == "eq_high") {
      set_player_eq(player_index, 2, value);
   } else if (name == "play_frame") {
      set_player_position_frame(player_index, value, true);
   } else if (name == "play_frame_relative") {
      set_player_position_frame(player_index, value, false);
   } else if (name == "play_beat_relative") {
      set_player_position_beat_relative(player_index, value);
   } else {
      //get the state for this name
      QHash<QString, int>::iterator state_itr = pstate->mParamInt.find(name);
      if (state_itr == pstate->mParamInt.end()) {
         cerr << DJ_FILEANDLINE << name.toStdString() << " is not a valid player_set (int) arg" << endl;
         return;
      }

      //return if there isn't anything to do
      if (value == *state_itr)
         return;

      //get the action
      QHash<QString, PlayerDoubleCommand::action_t>::const_iterator action_itr = mPlayerDoubleActionMapping.find(name);
      if (action_itr == mPlayerDoubleActionMapping.end()) {
         cerr << DJ_FILEANDLINE << name.toStdString() << " is not a valid player_set (int) arg [action not found]" << endl;
         return;
      }

      double dvalue = (double)value / double(one_scale);
      //speed comes in percent
      if (name == "speed") {
         //don't send speed commands if we are syncing or getting speed after going free
         if(pstate->mParamBool["sync"] || pstate->mPostFreeSpeedUpdates < 2)
            return;
         dvalue += 1;
      }

      //store value if it has changed
      if(pstate->mParamInt[name] != value)
         pstate->mParamInt[name] = value;
      else
         return;

      queue_command(new dj::audio::PlayerDoubleCommand(player_index, *action_itr, dvalue));
      emit(player_value_changed(player_index, name, value));
   }
}

void AudioModel::player_set(int player_index, QString name, double value) {
   if (player_index < 0 || player_index >= (int)mNumPlayers)
      return;

   if (name == "play_position") {
      if (value < 0.0)
         value = 0.0;
      queue_command(new dj::audio::PlayerPositionCommand(player_index, PlayerPositionCommand::PLAY, TimePoint(value)));
   } else if (name == "play_position_relative") {
      queue_command(new dj::audio::PlayerPositionCommand(player_index, PlayerPositionCommand::PLAY_RELATIVE, TimePoint(value)));
   } else {
      cerr << DJ_FILEANDLINE << name.toStdString() << " is not a valid player_set (double) arg" << endl;
      return;
   }
}

void AudioModel::player_set(int player_index, QString name, dj::audio::TimePoint value) {
   if (player_index < 0 || player_index >= (int)mNumPlayers)
      return;

   QMutexLocker lock(&mPlayerStatesMutex);
   PlayerState * pstate = mPlayerStates[player_index];

   //get the action
   QHash<QString, PlayerPositionCommand::position_t>::const_iterator action_itr = mPlayerPositionActionMapping.find(name);
   if (action_itr == mPlayerPositionActionMapping.end()) {
      cerr << DJ_FILEANDLINE << name.toStdString() << " is not a valid player_set (TimePoint) arg [action not found]" << endl;
      return;
   }
   
   if (name == "play") {
      queue_command(new dj::audio::PlayerPositionCommand(player_index, PlayerPositionCommand::PLAY, value));
   } else if (name == "play_relative") {
      queue_command(new dj::audio::PlayerPositionCommand(player_index, PlayerPositionCommand::PLAY_RELATIVE, value));
   } else {
      //get the state for this name
      QHash<QString, TimePoint>::iterator state_itr = pstate->mParamPosition.find(name);
      if (state_itr == pstate->mParamPosition.end()) {
         cerr << DJ_FILEANDLINE << name.toStdString() << " is not a valid player_set (TimePoint) arg" << endl;
         return;
      }
      //return if there isn't anything to be done
      if (*state_itr == value)
         return;

      //set the new value
      *state_itr = value;

      //queue the actual command [who's action is stored in the action_itr]
      queue_command(new dj::audio::PlayerPositionCommand(player_index, *action_itr, value));

      //XXX TODO emit(player_position_changed(player_index, name, value));
   }
}

void AudioModel::player_load(int player_index, QString audio_file_path, QString annotation_file_path) {
   if (player_index < 0 || player_index >= (int)mNumPlayers)
      return;
   QMutexLocker lock(&mPlayerStatesMutex);

   player_trigger(player_index, "clear");
   mThreadPool[player_index]->load(player_index, audio_file_path, annotation_file_path);
}

void AudioModel::start_audio() {
   mAudioIO->start();

   //mAudioIO->connectToPhysical(0,4);
   //mAudioIO->connectToPhysical(1,5);
   //mAudioIO->connectToPhysical(2,2);
   //mAudioIO->connectToPhysical(3,3);

   //XXX make this configurable
   mAudioIO->connectToPhysical(0,0);
   mAudioIO->connectToPhysical(1,1);
}

void AudioModel::stop_audio() {
   //there must be a better way than this!
   for(unsigned int i = 0; i < mNumPlayers; i++)
      player_trigger(i, "clear");
   usleep(500000);
   mAudioIO->stop();
   usleep(500000);
}

void AudioModel::queue_command(dj::audio::Command * cmd){
   mMaster->scheduler()->execute(cmd);
}

void AudioModel::player_eval_audible(int player_index) {
   if (player_index < 0 || player_index >= (int)mNumPlayers)
      return;

   QMutexLocker lock(&mPlayerStatesMutex);

   PlayerState * state = mPlayerStates[player_index];
   bool audible = true;

   if(state->mNumFrames == 0 ||
         state->mCurrentFrame >= state->mNumFrames ||
         state->mParamBool["mute"] ||
         state->mParamBool["pause"] ||
         //XXX detect player cue style: player->mParamBool["cue"]
         state->mParamInt["volume"] < mPlayerAudibleThresholdVolume ||
         (mCrossFadeEnabled && 
          ((mCrossFadePlayers[1] == player_index && mCrossFadePosition < mCrossfadeAudibleThresholdPosition) ||
          (mCrossFadePlayers[0] == player_index && mCrossFadePosition > (one_scale - mCrossfadeAudibleThresholdPosition))))
         ) {
      audible = false;
   }

   if (state->mParamBool["audible"] != audible) {
      state->mParamBool["audible"] = audible;
      emit(player_value_changed(player_index, "audible", audible));
   } 
}

