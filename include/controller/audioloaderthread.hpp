#ifndef DATAJOCKEY_AUDIO_LOADER_THREAD_H
#define DATAJOCKEY_AUDIO_LOADER_THREAD_H

#include <QThread>
#include <QMutex>
#include <QString>
#include "audiomodel.hpp"

namespace DataJockey {
   namespace Audio {

      class AudioBuffer;
      class AudioModel::AudioLoaderThread : public QThread {
         Q_OBJECT
         public:
            AudioLoaderThread(AudioModel * model);
            DataJockey::Audio::AudioBuffer * load(QString location);
            void run();
            static void progress_callback(int percent, void *objPtr);
            void abort();
            const QString& file_name();
         signals:
            void load_progress(QString fileName, int percent);
         protected:
            void relay_load_progress(QString fileName, int percent);
            DataJockey::Audio::AudioBuffer * mAudioBuffer;
            AudioModel * model();
         private:
            AudioModel * mAudioModel;
            QString mFileName;
            QMutex mMutex;
            bool mAborted;
      };
   }
}

#endif