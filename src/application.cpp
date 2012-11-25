#include "application.hpp"
#include "mixerpanel.hpp"
#include "midimapper.hpp"
#include "playerview.hpp"
#include "audiomodel.hpp"
#include "defines.hpp"
#include "beatbuffer.hpp"
#include "db.hpp"
#include "workdetailview.hpp"
#include "workdbview.hpp"
#include "worktablemodel.hpp"
#include "tagmodel.hpp"
#include "tageditor.hpp"
#include "oscreceiver.hpp"
#include "oscsender.hpp"
#include "config.hpp"
#include "annotation.hpp"
#include "appmainwindow.hpp"
#include "workrelationmodel.hpp"
#include "workfiltermodel.hpp"

#include <QSlider>
#include <QFile>
#include <QWidget>
#include <QSplitter>
#include <QTabWidget>
#include <QSqlRelationalTableModel>
#include <QSqlRelation>
#include <QDebug>
#include <QSettings>
#include <stdexcept>
#include <boost/program_options.hpp>

using namespace dj;
using std::endl;
using std::cout;

namespace {
  void connect_common_interfaces(QObject * src, QObject * dst, Qt::ConnectionType connection_type = Qt::AutoConnection) {
    QObject::connect(
        src, SIGNAL(player_triggered(int, QString)),
        dst, SLOT(player_trigger(int, QString)),
        connection_type);
    QObject::connect(
        src, SIGNAL(player_value_changed(int, QString, bool)),
        dst, SLOT(player_set(int, QString, bool)),
        connection_type);
    QObject::connect(
        src, SIGNAL(player_value_changed(int, QString, int)),
        dst, SLOT(player_set(int, QString, int)),
        connection_type);
    QObject::connect(
        src, SIGNAL(player_value_changed(int, QString, double)),
        dst, SLOT(player_set(int, QString, double)),
        connection_type);
    QObject::connect(
        src, SIGNAL(player_value_changed(int, QString, QString)),
        dst, SLOT(player_set(int, QString, QString)),
        connection_type);

    QObject::connect(
        src, SIGNAL(master_triggered(QString)),
        dst, SLOT(master_trigger(QString)),
        connection_type);
    QObject::connect(
        src, SIGNAL(master_value_changed(QString, bool)),
        dst, SLOT(master_set(QString, bool)),
        connection_type);
    QObject::connect(
        src, SIGNAL(master_value_changed(QString, int)),
        dst, SLOT(master_set(QString, int)),
        connection_type);
    QObject::connect(
        src, SIGNAL(master_value_changed(QString, double)),
        dst, SLOT(master_set(QString, double)),
        connection_type);
  }
}

Application::Application(int & argc, char ** argv) :
   QApplication(argc, argv),
   mCurrentwork(0)
{
   setApplicationVersion(dj::version_string);
   setApplicationName("Data Jockey " + applicationVersion());

   //for global qsettings
   QCoreApplication::setOrganizationName("xnor");
   QCoreApplication::setOrganizationDomain("x37v.info");
   QCoreApplication::setApplicationName("DataJockey");

   Configuration * config = Configuration::instance();

   model::db::setup(
         config->db_adapter(),
         config->db_name(),
         config->db_username(),
         config->db_password());

   mAudioModel = audio::AudioModel::instance();
   mTop = new MainWindow;
   mMixerPanel = new view::MixerPanel(mTop);

   mMIDIMapper = new controller::MIDIMapper(mTop);
   mOSCSender = new controller::OSCSender(mTop);

   setStyle("plastique");

   QObject::connect(mMixerPanel,
         SIGNAL(master_triggered(QString)),
         mAudioModel,
         SLOT(master_trigger(QString)));
   QObject::connect(mMixerPanel,
         SIGNAL(master_value_changed(QString, int)),
         mAudioModel,
         SLOT(master_set(QString, int)));
   QObject::connect(mAudioModel,
         SIGNAL(master_value_changed(QString, int)),
         mMixerPanel,
         SLOT(master_set(QString, int)));
   QObject::connect(mMixerPanel,
         SIGNAL(master_value_changed(QString, double)),
         mAudioModel,
         SLOT(master_set(QString, double)));
   QObject::connect(mAudioModel,
         SIGNAL(master_value_changed(QString, double)),
         mMixerPanel,
         SLOT(master_set(QString, double)));
   QObject::connect(mAudioModel,
         SIGNAL(master_value_changed(QString, TimePoint)),
         mMixerPanel,
         SLOT(master_set(QString, TimePoint)));

   //hook the trigger to us for loading only
   QObject::connect(mMixerPanel,
         SIGNAL(player_triggered(int, QString)),
         SLOT(player_trigger(int, QString)));

   //hook view into model
   QObject::connect(mMixerPanel,
         SIGNAL(player_triggered(int, QString)),
         mAudioModel,
         SLOT(player_trigger(int, QString)));
   QObject::connect(mMixerPanel,
         SIGNAL(player_value_changed(int, QString, bool)),
         mAudioModel,
         SLOT(player_set(int, QString, bool)));
   QObject::connect(mMixerPanel,
         SIGNAL(player_value_changed(int, QString, int)),
         mAudioModel,
         SLOT(player_set(int, QString, int)));

   QObject::connect(mAudioModel,
         SIGNAL(player_value_changed(int, QString, bool)),
         mMixerPanel,
         SLOT(player_set(int, QString, bool)));
   QObject::connect(mAudioModel,
         SIGNAL(player_value_changed(int, QString, int)),
         mMixerPanel,
         SLOT(player_set(int, QString, int)));

   QObject::connect(mAudioModel,
         SIGNAL(player_value_changed(int, QString, QString)),
         mMixerPanel,
         SLOT(player_set(int, QString, QString)));

   QObject::connect(mAudioModel,
         SIGNAL(player_buffers_changed(int, AudioBufferPtr, BeatBufferPtr)),
         mMixerPanel,
         SLOT(player_set_buffers(int, AudioBufferPtr, BeatBufferPtr)));

   mAudioModel->master_set("crossfade_enabled", true);
   mAudioModel->master_set("crossfade_player_left", 0);
   mAudioModel->master_set("crossfade_player_right", 1);
   mAudioModel->master_set("crossfade_position", (int)one_scale / 2);
   mAudioModel->master_set("bpm", 120.0);

   //hook up mapper
   //mapper out
   connect_common_interfaces(mMIDIMapper, mAudioModel);

   //mapper view
   QObject::connect(
         mAudioModel, SIGNAL(player_triggered(int, QString)),
         mMIDIMapper, SLOT(player_trigger(int, QString)),
         Qt::QueuedConnection);
   QObject::connect(
         mAudioModel, SIGNAL(player_value_changed(int, QString, bool)),
         mMIDIMapper, SLOT(player_set(int, QString, bool)),
         Qt::QueuedConnection);
   QObject::connect(
         mAudioModel, SIGNAL(player_value_changed(int, QString, int)),
         mMIDIMapper, SLOT(player_set(int, QString, int)),
         Qt::QueuedConnection);
   /*
   QObject::connect(
         mAudioModel, SIGNAL(player_value_changed(int, QString, double)),
         mMIDIMapper, SLOT(player_set(int, QString, double)),
         Qt::QueuedConnection);
         */

   QObject::connect(
         mAudioModel, SIGNAL(master_triggered(QString)),
         mMIDIMapper, SLOT(master_trigger(QString)),
         Qt::QueuedConnection);
   /*
   QObject::connect(
         mAudioModel, SIGNAL(master_value_changed(QString, bool)),
         mMIDIMapper, SLOT(master_set(QString, bool)),
         Qt::QueuedConnection);
         */
   QObject::connect(
         mAudioModel, SIGNAL(master_value_changed(QString, int)),
         mMIDIMapper, SLOT(master_set(QString, int)),
         Qt::QueuedConnection);
   QObject::connect(
         mAudioModel, SIGNAL(master_value_changed(QString, double)),
         mMIDIMapper, SLOT(master_set(QString, double)),
         Qt::QueuedConnection);

   QObject::connect(
         mMixerPanel, SIGNAL(midi_map_triggered()),
         mMIDIMapper, SLOT(map()),
         Qt::QueuedConnection);

   //mapper + view
   QObject::connect(
         mTop->midi_mapper(),
         SIGNAL(player_mapping_update(
               int, QString,
               midimapping_t, int, int,
               double, double)),
         mMIDIMapper,
         SLOT(map_player(
               int, QString,
               midimapping_t, int, int,
               double, double)),
         Qt::QueuedConnection);
   QObject::connect(
         mTop->midi_mapper(),
         SIGNAL(master_mapping_update(
               QString,
               midimapping_t, int, int,
               double, double)),
         mMIDIMapper,
         SLOT(map_master(
               QString,
               midimapping_t, int, int,
               double, double)),
         Qt::QueuedConnection);
   QObject::connect(
         mMIDIMapper,
         SIGNAL(player_mapping_update(
               int, QString,
               midimapping_t, int, int,
               double, double)),
         mTop->midi_mapper(),
         SLOT(map_player(
               int, QString,
               midimapping_t, int, int,
               double, double)),
         Qt::QueuedConnection);
   QObject::connect(
         mMIDIMapper,
         SIGNAL(master_mapping_update(
               QString,
               midimapping_t, int, int,
               double, double)),
         mTop->midi_mapper(),
         SLOT(map_master(
               QString,
               midimapping_t, int, int,
               double, double)),
         Qt::QueuedConnection);
   QObject::connect(
         mTop->midi_mapper(), SIGNAL(requesting_mappings()),
         mMIDIMapper, SLOT(query()),
         Qt::QueuedConnection);

   //osc sender
   connect_common_interfaces(mAudioModel, mOSCSender, Qt::QueuedConnection);

   QObject::connect(this, SIGNAL(aboutToQuit()), mOSCSender, SLOT(send_quit()));

   TagModel * tag_model = new TagModel(model::db::get(), mTop);
   WorkDetailView * work_detail = new WorkDetailView(tag_model, model::db::get(), mTop);

   WorkRelationModel * rtable_model = new WorkRelationModel("works", mTop, model::db::get());
   rtable_model->select();

   QList<WorkDBView *> db_views;
   QStringList db_view_names;
   WorkDBView * work_db_view = new WorkDBView(rtable_model, mTop);
   db_views << work_db_view;
   db_view_names << "all";

   QStringList tag_names;
   tag_names << "aug282012" << "jams" << "halloween" << "minimal_synth";

   foreach(const QString tag, tag_names) {
     //QString filtered_name = model::db::work::filtered_table_by_tag(tag);
     //rtable_model = new WorkRelationModel(filtered_name, mTop, model::db::get());

     WorkFilterModel * ftable_model = new WorkFilterModel(mTop, model::db::get());
     QString tag_id;
     tag_id.setNum(model::db::tag::find(tag));
     ftable_model->set_filter_expression("audio_work_tags.tag_id = " + tag_id);
     ftable_model->select();
     db_views << new WorkDBView(ftable_model, mTop);
     db_view_names << tag;
   }

   //XXX hack to write settings before quitting, is there a better way?
   QObject::connect(
         this, SIGNAL(aboutToQuit()),
         work_db_view, SLOT(write_settings()));

   TagEditor * tag_editor = new TagEditor(tag_model, mTop);

   foreach (WorkDBView * view, db_views) {
     //work selection
     QObject::connect(
         view,
         SIGNAL(workSelected(int)),
         work_detail,
         SLOT(setWork(int)));

     QObject::connect(
         view,
         SIGNAL(workSelected(int)),
         SLOT(select_work(int)));
   }

   QTabWidget * left_tab_view = new QTabWidget(mTop);
   left_tab_view->addTab(mMixerPanel, "mixer");
   left_tab_view->addTab(tag_editor, "tags");
   //left_tab_view->addTab(filter_list_view, "filters");

   QBoxLayout * left_layout = new QBoxLayout(QBoxLayout::TopToBottom);
   QSplitter * splitter = new QSplitter(Qt::Vertical);
   splitter->addWidget(left_tab_view);
   splitter->addWidget(work_detail);
   left_layout->addWidget(splitter);


   QBoxLayout * top_layout = new QBoxLayout(QBoxLayout::LeftToRight);
   splitter = new QSplitter(Qt::Horizontal);
   QWidget * left = new QWidget;
   left->setLayout(left_layout);
   splitter->addWidget(left);

   QTabWidget * right_tab_view = new QTabWidget(mTop);
   for (int i = 0; i < db_views.size(); i++) {
     right_tab_view->addTab(db_views[i], db_view_names[i]);
   }
   splitter->addWidget(right_tab_view);

   top_layout->addWidget(splitter);

   //set the default sizes
   QList<int> sizes;
   sizes << mMixerPanel->minimumWidth();
   sizes << right_tab_view->maximumWidth() - mMixerPanel->minimumWidth();
   splitter->setSizes(sizes);


   //open files, load defaults
   QFile file(":/resources/style.qss");
   if(file.open(QFile::ReadOnly)){
      QString styleSheet = QLatin1String(file.readAll());
      this->setStyleSheet(styleSheet);
   }

   QString midi_mapping_file = config->midi_mapping_file();
   if (QFile::exists(midi_mapping_file))
      mMIDIMapper->load_file(midi_mapping_file);
   if (config->midi_mapping_auto_save())
      mMIDIMapper->auto_save(midi_mapping_file);
   else
      mMIDIMapper->auto_save(false);


   //start up threads
   mAudioModel->start_audio();

   OscThread * osc_thread = new OscThread(config->osc_in_port());
   osc_thread->start();
   connect_common_interfaces(osc_thread->receiver(), mAudioModel, Qt::QueuedConnection);

   mMIDIMapper->start();
   mOSCSender->start();

   QWidget * central = new QWidget(mTop);
   central->setLayout(top_layout);

   QObject::connect(this, SIGNAL(aboutToQuit()), SLOT(pre_quit_actions()));

   mTop->setCentralWidget(central);
   mTop->show();
}

void Application::pre_quit_actions() {
   sleep(1);
   mAudioModel->stop_audio();
   model::db::close();
}

void Application::select_work(int work_id) {
   mCurrentwork = work_id;
}

void Application::player_trigger(int player_index, QString name) {
	if (name != "load")
		return;

	//find the file locations
	QString audio_file;
	QString annotation_file;
	if (!model::db::find_locations_by_id(mCurrentwork, audio_file, annotation_file))
		return;
   mAudioModel->player_load(
         player_index,
         audio_file,
         annotation_file);
   //doesn't actually do anything other than cause the model to emit
   mAudioModel->player_set(player_index, "load", mCurrentwork);

	//find the work info
	QString artist_name;
	QString work_title;
	if (model::db::find_artist_and_title_by_id(mCurrentwork, artist_name, work_title)) {
		//XXX invoke or is this okay?
		mMixerPanel->player_set(player_index,
				"song_description",
				artist_name + "\n" + work_title);
	} else
		mMixerPanel->player_set(player_index, "song_description", "unknown");
}

namespace po = boost::program_options;

int main(int argc, char * argv[]){
   try {
      dj::Configuration * config = dj::Configuration::instance();

      po::options_description cmdline_options("Allowed options");
      cmdline_options.add_options()
         ("help,h", "print this help message")
         ("config,c", po::value<std::string>(), "specify a config file")
         ;

      po::variables_map vm;
      po::store(po::command_line_parser(argc, argv).
            options(cmdline_options).run(), vm);
      po::notify(vm);

      if (vm.count("help")) {
         cout << endl << "usage: " << argv[0] << " [options] " << endl << endl;
         cout << cmdline_options << endl;
         return 1;
      }

      if (vm.count("config"))
         config->load_file(QString::fromStdString(vm["config"].as<std::string>()));
      else
         config->load_default();

      int fake_argc = 1; //only passing the program name because we use the rest with boost
      dj::Application app(fake_argc, argv);
      app.exec();

   } catch(std::exception& e) {
      cout << "error ";
      cout << e.what() << endl;
      return 1;
   }
}

