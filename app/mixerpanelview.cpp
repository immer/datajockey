#include "mixerpanelview.h"
#include "ui_mixerpanelview.h"
#include "playerview.h"

MixerPanelView::MixerPanelView(QWidget *parent) :
  QWidget(parent),
  ui(new Ui::MixerPanelView)
{
  ui->setupUi(this);
  mPlayerViews.push_back(ui->leftPlayer);
  mPlayerViews.push_back(ui->rightPlayer);

  //connect up the players
  for (int i = 0; i < mPlayerViews.size(); i++) {
    PlayerView * player = mPlayerViews[i];
    connect(player, 
        &PlayerView::valueChangedDouble,
        [this, i] (QString name, double v) { emit(playerValueChangedDouble(i, name, v)); });
    connect(player, 
        &PlayerView::valueChangedInt,
        [this, i] (QString name, int v) { emit(playerValueChangedInt(i, name, v)); });
    connect(player, 
        &PlayerView::valueChangedBool,
        [this, i] (QString name, bool v) { emit(playerValueChangedBool(i, name, v)); });
    connect(player, 
        &PlayerView::triggered,
        [this, i] (QString name) { emit(playerTriggered(i, name)); });
  }

  connect(ui->volume, &QSlider::valueChanged,
      [this] (int value) { emit(masterValueChangedInt("volume", value)); });
  connect(ui->tempoBox, 
      static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged),
      [this] (double value) { emit(masterValueChangedDouble("bpm", value)); });
}

MixerPanelView::~MixerPanelView()
{
  delete ui;
}

void MixerPanelView::playerSetWorkInfo(int player, QString info) {
  if (!inRange(player))
    return;
  mPlayerViews[player]->setWorkInfo(info);
}

void MixerPanelView::playerSetValueInt(int player, QString name, int value) {
  if (!inRange(player))
    return;
  mPlayerViews[player]->setValueInt(name, value);
}

void MixerPanelView::playerSetValueBool(int player, QString name, bool value) {
  if (!inRange(player))
    return;
  mPlayerViews[player]->setValueBool(name, value);
}

void MixerPanelView::playerSetValueDouble(int player, QString name, double value) {
  if (!inRange(player))
    return;
  mPlayerViews[player]->setValueDouble(name, value);
}

bool MixerPanelView::inRange(int player) {
  return player >= 0 && player < mPlayerViews.size();
}

