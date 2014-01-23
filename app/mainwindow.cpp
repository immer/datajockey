#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "db.h"
#include "audiomodel.h"
#include "audioloader.h"
#include "mixerpanelview.h"

#include <QSqlQueryModel>

MainWindow::MainWindow(DB *db, AudioModel * audio, QWidget *parent) :
  QMainWindow(parent),
  ui(new Ui::MainWindow),
  mDB(db)
{
  ui->setupUi(this);
  ui->allWorks->setDB(db);
  MixerPanelView * mixer = ui->mixer;
  connect(mixer, &MixerPanelView::playerValueChangedDouble, audio, &AudioModel::playerSetValueDouble);
  connect(mixer, &MixerPanelView::playerValueChangedInt,    audio, &AudioModel::playerSetValueInt);
  connect(mixer, &MixerPanelView::playerValueChangedBool,   audio, &AudioModel::playerSetValueBool);
  connect(mixer, &MixerPanelView::playerTriggered,          audio, &AudioModel::playerTrigger);

  connect(mixer, &MixerPanelView::masterValueChangedDouble, audio, &AudioModel::masterSetValueDouble);
  connect(mixer, &MixerPanelView::masterValueChangedInt,    audio, &AudioModel::masterSetValueInt);
  connect(mixer, &MixerPanelView::masterValueChangedBool,   audio, &AudioModel::masterSetValueBool);
  connect(mixer, &MixerPanelView::masterTriggered,          audio, &AudioModel::masterTrigger);

  connect(audio, &AudioModel::playerValueChangedDouble, mixer, &MixerPanelView::playerSetValueDouble);
  connect(audio, &AudioModel::playerValueChangedInt,    mixer, &MixerPanelView::playerSetValueInt);
  connect(audio, &AudioModel::playerValueChangedBool,   mixer, &MixerPanelView::playerSetValueBool);

  connect(audio, &AudioModel::masterValueChangedDouble, mixer, &MixerPanelView::masterSetValueDouble);
  connect(audio, &AudioModel::masterValueChangedInt,    mixer, &MixerPanelView::masterSetValueInt);
  connect(audio, &AudioModel::masterValueChangedBool,   mixer, &MixerPanelView::masterSetValueBool);

  ui->topSplitter->setStretchFactor(0,0);
  ui->topSplitter->setStretchFactor(1,1);
  ui->leftSplitter->setStretchFactor(0,1);
  ui->leftSplitter->setStretchFactor(1,0);
}

void MainWindow::loader(AudioLoader * loader) {
  MixerPanelView * mixer = ui->mixer;
  connect(mixer, &MixerPanelView::playerTriggered, loader, &AudioLoader::playerTrigger);
  connect(ui->allWorks, &WorksTableView::workSelected, loader, &AudioLoader::selectWork);
  ui->allWorks->setDB(mDB);

  connect(loader, &AudioLoader::playerValueChangedInt, mixer, &MixerPanelView::playerSetValueInt);
  connect(loader, &AudioLoader::playerValueChangedString, mixer, &MixerPanelView::playerSetValueString);

  connect(loader, &AudioLoader::playerBuffersChanged, mixer, &MixerPanelView::playerSetBuffers);
}

MainWindow::~MainWindow()
{
  delete ui;
}
