#include "renameabletabwidget.h"

#include <QInputDialog>
#include <QMouseEvent>

RenameableTabWidget::RenameableTabWidget(QWidget * parent) :
  QTabWidget(parent)
{
  RenameableTabBar * bar = new RenameableTabBar(this);
  setTabBar(bar);
}

RenameableTabBar::RenameableTabBar(QWidget *parent) :
  QTabBar(parent)
{
}

void RenameableTabBar::mouseDoubleClickEvent(QMouseEvent *e) {
  if (e->button () != Qt::LeftButton) {
    QTabBar::mouseDoubleClickEvent(e);
    return;
  }

  int idx = currentIndex();
  bool ok = true;
  QString newName = QInputDialog::getText(
      this, tr("Change Name"),
      tr("Insert New Tab Name"),
      QLineEdit::Normal,
      tabText (idx),
      &ok);

  if (ok)
    setTabText (idx, newName);
}

