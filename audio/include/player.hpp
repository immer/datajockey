#ifndef DATAJOCKEY_PLAYER_HPP
#define DATAJOCKEY_PLAYER_HPP

#include "command.hpp"
#include "timepoint.hpp"
#include "transport.hpp"
#include "jackaudioio.hpp"
#include "audiobuffer.hpp"
#include "beatbuffer.hpp"
#include "rubberband/RubberBandStretcher.h"

namespace DataJockey {
   namespace Audio {
      class Player {
         public:
            //internal types
            enum play_state_t {PLAY, PAUSE};
            enum out_state_t {MAIN_MIX, CUE};
            enum stretch_method_t {PLAY_RATE, RUBBER_BAND};

            Player();
            ~Player();

            //this creates internal buffers
            //** must be called BEFORE the audio callback starts
            void setup_audio(
                  unsigned int sampleRate,
                  unsigned int maxBufferLen);

            //the audio computation methods
            //the player doesn't own its own buffer, it is passed it..
            //but it does own it between pre_compute and fill_output
            //setup for audio computation, we will be computing numFrames
            void audio_pre_compute(unsigned int numFrames, float ** mixBuffer,
                  const Transport& transport); 
            //actually compute one frame, filling an internal buffer
            //syncing to the transport if mSync == true,
            //inbeat reflects if the transport computed a new beat on the last
            //tick
            void audio_compute_frame(unsigned int frame, float ** mixBuffer, 
                  const Transport& transport, bool inbeat); 
            //finalize audio computation, apply effects, etc.
            void audio_post_compute(unsigned int numFrames, float ** mixBuffer); 
            //actually fill the output vectors
            void audio_fill_output_buffers(unsigned int numFrames, 
                  float ** mixBuffer, float ** cueBuffer);

            //getters
            play_state_t play_state();
            out_state_t out_state();
            stretch_method_t stretch_method();
            bool muted();
            bool syncing();
            bool looping();
            double volume();
            double play_speed();
            const TimePoint& position();
            const TimePoint& start_position();
            const TimePoint& end_position();
            const TimePoint& loop_start_position();
            const TimePoint& loop_end_position();

            //setters
            void play_state(play_state_t val);
            void out_state(out_state_t val);
            void stretch_method(stretch_method_t val);
            void mute(bool val);
            void sync(bool val);
            void loop(bool val);
            void volume(double val);
            void play_speed(double val);
            void position(const TimePoint &val);
            void start_position(const TimePoint &val);
            void end_position(const TimePoint &val);
            void loop_start_position(const TimePoint &val);
            void loop_end_position(const TimePoint &val);
            void audio_buffer(AudioBuffer * buf);
            void beat_buffer(BeatBuffer * buf);

            //misc
            void position_relative(TimePoint amt); //go to a position relative to the current position
            void play_speed_relative(double amt); //increment or decrement the current play speed by amt
            void volume_relative(double amt); //increment or decrement the current volume

         private:
            //states
            play_state_t mPlayState;
            out_state_t mOutState;
            stretch_method_t mStretchMethod;
            bool mMute;
            bool mSync; //sync to main transport or not
            bool mLoop;
            bool mSetup;

            //continuous
            double mVolume;
            double mPlaySpeed;
            TimePoint mPosition; //the current position in the audio
            bool mPositionDirty; //indicates if the sampleindex needs update based
            //on the position update
            TimePoint mStartPosition; //where we start the playback
            TimePoint mEndPosition; //where we end the playback
            TimePoint mLoopStartPosition;
            TimePoint mLoopEndPosition;

            //internals, bookkeeping, etc
            unsigned int mSampleRate;
            float * mVolumeBuffer;
            unsigned long mSampleIndex;
            double mSampleIndexResidual;
            RubberBand::RubberBandStretcher * mRubberBandStretcher;
            AudioBuffer * mAudioBuffer;
            BeatBuffer * mBeatBuffer;

            //helpers
            void update_position(const Transport& transport);
      };
      class PlayerCommand : public Command {
         public:
            PlayerCommand(unsigned int idx);
            //getters
            unsigned int index() const;
            const TimePoint& position_executed() const;
            //setters
            void position_executed(TimePoint const & t);
            //misc
            void store(CommandIOData& data, const std::string& name) const;
         protected:
            Player * player();
         private:
            unsigned int mIndex;
            //this comes from the player's position
            TimePoint mPositionExecuted;
      };
      class PlayerStateCommand : public PlayerCommand {
         public:
            enum action_t {
               PLAY, PAUSE,
               OUT_MAIN, OUT_CUE,
               SYNC, NO_SYNC,
               MUTE, NO_MUTE,
               LOOP, NO_LOOP
            };
            PlayerStateCommand(unsigned int idx, action_t action);
            virtual void execute();
            virtual bool store(CommandIOData& data) const;
         private:
            action_t mAction;
      };
      class PlayerDoubleCommand : public PlayerCommand {
         public:
            enum action_t {
               VOLUME, VOLUME_RELATIVE,
               PLAY_SPEED, PLAY_SPEED_RELATIVE
            };
            PlayerDoubleCommand(unsigned int idx, action_t action, double value);
            virtual void execute();
            virtual bool store(CommandIOData& data) const;
         private:
            action_t mAction;
            double mValue;
      };
      class PlayerLoadCommand : public PlayerCommand {
         public:
            PlayerLoadCommand(unsigned int idx, AudioBuffer * buffer, BeatBuffer * beatBuffer = NULL);
            virtual void execute();
            virtual bool store(CommandIOData& data) const;
         private:
            AudioBuffer * mAudioBuffer;
            BeatBuffer * mBeatBuffer;
      };
      class PlayerPositionCommand : public PlayerCommand {
         public:
            enum position_t {
               PLAY, PLAY_RELATIVE, 
               START, END, LOOP_START, LOOP_END
            };
            PlayerPositionCommand(unsigned int idx, position_t target, const TimePoint & timepoint);
            virtual void execute();
            virtual bool store(CommandIOData& data) const;
         private:
            position_t mTarget;
            TimePoint mTimePoint;
      };
   }
}

#endif