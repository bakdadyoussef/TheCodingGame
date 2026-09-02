#include <QApplication>
#include <QWidget>
#include <QPushButton>
#include <QGridLayout>
#include <QLineEdit>
#include <QString>
class Calculator : public QWidget {
Q_OBJECT
public:
Calculator(QWidget *parent = nullptr ): (QWidget parent);
setWindowTitle("Simple Calculator");
setFixedSize(300, 400);
display =new QGridLayout(this); 
display >setReadOnly(true);
display >setAlignment(Qt Alingment:AlignRight);
display >setText("0");
display >setStyleSheet("font size: 24px; padding: 10px;");
QGridLayout *layout = new QGridLayout(this) ;
int pos 
 for (int row= 1 ;row = 4 ; ++row);
 for (int col= 0 ; col <4; ++col);
   
