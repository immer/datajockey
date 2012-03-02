#ifndef PLAYER_VIEW_HPP
#define PLAYER_VIEW_HPP

#include <QWidget>
#include <QMap>
#include <QString>
#include <QBoxLayout>
#include <QTextEdit>
#include <QGraphicsView>

class QPushButton;
class QSlider;
class QDial;
class QProgressBar;
class QGraphicsView;

namespace DataJockey {
   namespace View {
      class WaveFormItem;

      class Player : public QWidget {
         Q_OBJECT
         public:
            enum WaveformOrientation { WAVEFORM_NONE, WAVEFORM_LEFT, WAVEFORM_RIGHT };
            Player(QWidget * parent = NULL, WaveformOrientation waveform_orientation = WAVEFORM_RIGHT);
            QPushButton * button(QString name) const;
            QDial * eq_dial(QString name) const;
            QSlider * volume_slider() const;
            QProgressBar * progress_bar() const;
         private:
            QProgressBar * mProgressBar;
            QMap<QString, QPushButton *> mButtons;
            QBoxLayout * mTopLayout;
            QBoxLayout * mControlLayout;
            QTextEdit * mTrackDescription;
            QSlider * mVolumeSlider;
            QMap<QString, QDial *> mEqDials;
            WaveFormItem * mWaveForm;
            QGraphicsView * mWaveFormView;
      };
   }
}

#endif
