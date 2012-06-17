#ifndef DATAJOCKEY_APPLICATION_HPP
#define DATAJOCKEY_APPLICATION_HPP

#include <QApplication>
#include "mixerpanel.hpp"

class QWidget;

namespace dj {
   namespace audio { class AudioModel; }
   namespace controller { class MIDIMapper; }
   class Application : public QApplication {
      Q_OBJECT
      public:
         Application(int & argc, char ** argv);
      public slots:
         void pre_quit_actions();
         void select_work(int work_id);
         void set_player_trigger(int player_index, QString name);
      private:
         audio::AudioModel * mAudioModel;
         view::MixerPanel * mMixerPanel;
         controller::MIDIMapper * mMIDIMapper;
         QWidget * mTop;
         int mCurrentwork;
   };
}

#endif
