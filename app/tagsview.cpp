#include "tagsview.h"
#include "ui_tagsview.h"
#include "tagmodel.h"

TagsView::TagsView(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::TagsView)
{
  ui->setupUi(this);
}

TagsView::~TagsView()
{
  delete ui;
}

void TagsView::setModel(QAbstractItemModel * model) {
  ui->tree->setModel(model);
  ui->tree->setHeaderHidden(true);
  ui->tree->setColumnHidden(TagModel::idColumn(), true);
  ui->tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
  ui->tree->setDragEnabled(true);
}

