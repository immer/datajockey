#ifndef DATAJOCKEY_PLAYER_MAPPER_HPP
#define DATAJOCKEY_PLAYER_MAPPER_HPP

#include <QObject>
#include <QMap>
#include "audiomodel.hpp"

class QPushButton;
class QSlider;
class QDial;

namespace DataJockey {
   namespace View { class Player; }

   namespace Controller {
      //maps gui <-> model
      class PlayerMapper : public QObject {
         Q_OBJECT
         public:
            PlayerMapper(QObject * parent = NULL);
            virtual ~PlayerMapper();
            void map(int index, View::Player * player);
         protected slots:
            //uses button names
            void button_pressed();
            void button_toggled(bool state);

            void volume_changed(int value);
            void volume_changed(int player_index, int value);

            void eq_changed(int value);

            void file_changed(int player_index, QString file_name);
            void file_cleared(int player_index);
            void file_load_progress(int player_index, int percent);

            void position_changed(int player_index, int frame);

            void seek_relative(int frames);
            void seeking(bool start);

         private:
            QMap<View::Player *, int> mPlayerIndexMap;
            QMap<int, View::Player *> mIndexPlayerMap;

            QMap<QPushButton *, int> mButtonIndexMap;
            QMap<int, QPushButton *> mIndexButtonMap;

            QMap<QSlider *, int> mSliderIndexMap;
            QMap<int, QSlider *> mIndexSliderMap;

            QMap<QDial *, int> mDialIndexMap;
            QMap<int, QDial *> mIndexDialMap;

            QMap<int, bool> mIndexPreSeekPauseState;

            Audio::AudioModel * mAudioModel;
      };
   }
}

#endif