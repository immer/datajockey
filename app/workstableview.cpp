#include "workstableview.h"
#include "db.h"
#include <QSqlQueryModel>
#include <QHeaderView>
#include <QSortFilterProxyModel>

#include <iostream>
using namespace std;

WorksTableView::WorksTableView(QWidget *parent) :
  QTableView(parent)
{
}

void WorksTableView::setDB(DB * db) {
  mDB = db;

  QSqlQueryModel *model = new QSqlQueryModel(this);
  model->setQuery(db->work_table_query(), db->get());

  //QSortFilterProxyModel * sortable = new WorksSortFilterProxyModel(model);
  QSortFilterProxyModel * sortable = new QSortFilterProxyModel(model);
  sortable->setSourceModel(model);
  sortable->setSortCaseSensitivity(Qt::CaseInsensitive);

  setModel(sortable);

  //hide the id
  setColumnHidden(0, true);
  setSortingEnabled(true);
  horizontalHeader()->setSectionsMovable(true);
  //mTableView->horizontalHeader()->resizeSections(QHeaderView::ResizeToContents);
  verticalHeader()->setVisible(false);
  setSelectionBehavior(QAbstractItemView::SelectRows);
  setSelectionMode(QAbstractItemView::SingleSelection);
  //XXX actually do something with editing at some point
  //setEditTriggers(QAbstractItemView::DoubleClicked);
  
  connect(this, &QAbstractItemView::clicked, [this](const QModelIndex& index) {
      int workid = index.sibling(index.row(), 0).data().toInt();
      emit(workSelected(workid));
      });
}

WorksTableView::~WorksTableView() { }

WorksSortFilterProxyModel::WorksSortFilterProxyModel(QObject * parent) : QSortFilterProxyModel(parent)
{
}

bool WorksSortFilterProxyModel::lessThan(const QModelIndex& left, const QModelIndex& right) const {
  QVariant leftData = sourceModel()->data(left);
  QVariant rightData = sourceModel()->data(right);
  const int row = left.row();
  const int column = left.column();
  switch (column) {
    case DB::WORK_ARTIST_NAME:
      {
        QString leftArtist = leftData.toString();
        QString rightArtist = rightData.toString();
        if (leftArtist != rightArtist)
          return QString::localeAwareCompare(leftArtist, rightArtist) < 0;
      }
        //intentional drop through
    case DB::WORK_ALBUM_NAME:
      {
        QString leftAlbum = sourceModel()->data(left.sibling(row, DB::WORK_ALBUM_NAME)).toString();
        QString rightAlbum = sourceModel()->data(right.sibling(row, DB::WORK_ALBUM_NAME)).toString();
        if (leftAlbum != rightAlbum)
          return QString::localeAwareCompare(leftAlbum, rightAlbum) < 0;
      }
    case DB::WORK_ALBUM_TRACK:
      {
        int leftTrack = sourceModel()->data(left.sibling(row, DB::WORK_ALBUM_TRACK)).toInt();
        int rightTrack = sourceModel()->data(right.sibling(row, DB::WORK_ALBUM_TRACK)).toInt();
        if (leftTrack != rightTrack)
          return leftTrack < rightTrack;
        QString leftName = sourceModel()->data(left.sibling(row, DB::WORK_NAME)).toString();
        QString rightName = sourceModel()->data(right.sibling(row, DB::WORK_NAME)).toString();
        return QString::localeAwareCompare(leftName, rightName) < 0;
      }
      break;
      //work name is fine, just alphabetical
    case DB::WORK_NAME:
    default:
      break;
  }
  return QSortFilterProxyModel::lessThan(left, right);
}

