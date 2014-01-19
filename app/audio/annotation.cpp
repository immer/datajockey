#include "annotation.hpp"
#include "defines.hpp"
#include "config.hpp"
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QDir>

#include <yaml-cpp/yaml.h>
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <iostream>

using namespace djaudio;
namespace fs = boost::filesystem;

using std::cerr;
using std::endl;

namespace {
   void recursively_add(YAML::Emitter& yaml, const QHash<QString, QVariant>& attributes) {
      foreach (const QString &key, attributes.keys()) {
         yaml << YAML::Key << key.toStdString();
         yaml << YAML::Value;

         QVariant val = attributes.value(key);
         switch (static_cast<QMetaType::Type>(val.type())) {
            case QMetaType::QVariantHash:
               yaml << YAML::BeginMap;
               recursively_add(yaml, val.toHash());
               yaml << YAML::EndMap;
               break;
            case QMetaType::Float:
               yaml << val.toFloat();
               break;
            case QMetaType::Double:
               yaml << val.toDouble();
               break;
            case QMetaType::Int:
               yaml << val.toInt();
               break;
            case QMetaType::UInt:
               yaml << val.toUInt();
               break;
            case QMetaType::QString:
               yaml << val.toString().toStdString();
               break;
            default:
               yaml << YAML::Null;
               qDebug() << QString::fromStdString(DJ_FILEANDLINE) << " type: " << val.typeName() << " not supported";
               continue;
         }
      }
   }

   QString fmt_string(const QString& input) {
      QString ret = input.toLower();
      ret = ret.replace(QRegExp("\\s\\s*"), "_");
      ret = ret.replace('\\', '_');
      ret = ret.replace('/', '_');
      ret = ret.replace('~', '_');
      ret = ret.replace(QRegExp("\\.+"), "_");
      return QDir::cleanPath(ret);
   }
}

bool Annotation::loadFile(QString& file_path) {
  //clear();
  if (mBeatBuffer) 
    mBeatBuffer.reset();
  mBeatBuffer = new BeatBuffer();
  try {
    fs::ifstream fin(file_path.toStdWString());
    YAML::Parser parser(fin);
    YAML::Node doc;
    parser.GetNextDocument(doc);

    const YAML::Node& locs = doc["beat_locations"];
    if (locs.Type() == YAML::NodeType::Sequence && locs.size() == 0) {
      return false;
    } else {
      //XXX just using the last in the list
      const YAML::Node& beats = (locs.Type() == YAML::NodeType::Sequence) ? locs[locs.size() - 1]["time_points"] : locs["time_points"];
      for (unsigned int i = 0; i < beats.size(); i++) {
        double beat;
        beats[i] >> beat;
        mBeatBuffer->push_back(beat * 44100.0); //XXX
      }
    }
  } catch(...) {
    cerr << "problem loading " << qPrintable(file_path) << endl;
    return false;
  }
  return true;
}

void Annotation::update_attributes(QHash<QString, QVariant>& attributes) {
   //XXX deal with descriptors
   foreach (const QString &str, attributes.keys()) {
      //flat genre -> tags: - genre: value
      if (str == "genre") {
         QHash<QString, QVariant> tags;
         if (mAttrs.contains("tags"))
            tags = mAttrs["tags"].toHash();
         tags[str] = attributes.value(str).toString().toLower();
         mAttrs["tags"] = tags;
      //flat album/track to album: name: v, track: n
      } else if (
            (str == "album" && static_cast<QMetaType::Type>(attributes.value(str).type()) != QMetaType::QVariantHash) ||
            str == "track") {
         QHash<QString, QVariant> album;
         if (mAttrs.contains("album"))
            album = mAttrs["album"].toHash();
         album.insert(str == "album" ? "name" : str, attributes.value(str));
         mAttrs["album"] = album;
      } else {
         mAttrs[str] = attributes.value(str);
      }
   }
}

void Annotation::beat_buffer(BeatBufferPtr buffer) {
   mBeatBuffer = buffer;
}

void Annotation::write_file(const QString& file_path) throw(std::runtime_error) {
   YAML::Emitter yaml;
   yaml << YAML::BeginMap;

   //add attributes
   recursively_add(yaml, mAttrs);

   //add beat buffer
   if (mBeatBuffer && mBeatBuffer->size()) {
      yaml << YAML::Key << "beat_locations";
      yaml << YAML::Value << YAML::BeginMap << YAML::Key << "time_points";
      yaml << YAML::Value << YAML::BeginSeq;
      for (BeatBuffer::const_iterator it = mBeatBuffer->begin(); it != mBeatBuffer->end(); it++)
         yaml << *it;
      yaml << YAML::EndSeq << YAML::EndMap;
   }

   yaml << YAML::EndMap;

   //make sure the containing directory exists
   QFileInfo info(file_path);
   if (!info.dir().exists())
      info.dir().mkpath(info.dir().path());

   QFile file(file_path);
   if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
      throw(std::runtime_error(DJ_FILEANDLINE + " cannot open file: " + file_path.toStdString()));
   QTextStream out(&file);
   out << QString(yaml.c_str());
}

QString Annotation::default_file_location(int work_id) {
   QDir dir(dj::Configuration::instance()->annotation_dir());
   QString file_name;
   file_name.setNum(work_id);

   QHash<QString, QVariant>::const_iterator i;

   i = mAttrs.find("artist");
   if (i != mAttrs.end())
      file_name.append("-" + fmt_string(i->toString().toLower()));
   
   i = mAttrs.find("album");
   if (i != mAttrs.end()) {
      QHash<QString, QVariant> album = i->toHash();
      QHash<QString, QVariant>::const_iterator j = album.find("name");
      if (j != album.end())
         file_name.append("-" + fmt_string(j->toString().toLower()));
   }

   i = mAttrs.find("name");
   if (i != mAttrs.end())
      file_name.append("-" + fmt_string(i->toString()));

   file_name.append(".yaml");

   return dir.filePath(file_name);
}

