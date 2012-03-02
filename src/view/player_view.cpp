#include "player_view.hpp"
#include <QPushButton>
#include <QGridLayout>
#include "defines.hpp"

using namespace DataJockey::View;

struct button_info {
   int row;
   int col;
   bool checkable;
   QString label;
   QString name;
};

Player::Player(QWidget * parent) : QWidget(parent) {
   button_info items[] = {
      {0, 0, false, "ld", "load"},
      {0, 2, false, "rs", "reset"},
      {1, 0, true,  "cu", "cue"},
      {1, 1, true,  "sy", "sync"},
      {1, 2, true,  "pl", "play"},
      {2, 0, false, "<<", "seek_back"},
      {2, 2, false, ">>", "seek_forward"},
   };

   mTopLayout = new QBoxLayout(QBoxLayout::TopToBottom, this);
   QGridLayout * button_layout = new QGridLayout();

   mTrackDescription = new QTextEdit(this);
   mTrackDescription->setReadOnly(true);
   mTrackDescription->setText("EMPTY");
   mTopLayout->addWidget(mTrackDescription);

   for (unsigned int i = 0; i < sizeof(items) / sizeof(button_info); i++) {
      QPushButton * btn = new QPushButton(items[i].label, this);
      btn->setCheckable(items[i].checkable);
      mButtons.insert(items[i].name, btn);
      button_layout->addWidget(btn, items[i].row, items[i].col);
   }
   mTopLayout->addLayout(button_layout);

   mVolumeSlider = new QSlider(Qt::Vertical, this);
   mVolumeSlider->setRange(0, (int)(1.5 * (float)one_scale));
   mVolumeSlider->setValue(one_scale);
   mTopLayout->addWidget(mVolumeSlider, 1, Qt::AlignHCenter);

   setLayout(mTopLayout);
}

QPushButton * Player::button(QString name) const { return mButtons[name]; }
QSlider * Player::volume_slider() const { return mVolumeSlider; }

