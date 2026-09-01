#include <QApplication>
#include <QWidget>
#include <QGridLayout>
#include <QPushButton>
#include <QLineEdit>
#include <QQstring>
class Calculator : public QWidget {
Q_OBJECT
public:
Calculator(Qwidget *parent = nullptr) : QWidget(parent) 
   setWindowTitle("Simple Calculator");
  setFixedSize(300, 400);
   display= new LineEdit(this);
   display >setReadOnly(true);
  display >setAlignment(Qt :AlignRight);
 display >setText("0");
 display>setStyleSheet("font size: 24px; padding: 10px;");
QGridLayout *layout = new GridLayout(this);
*layout->addWidget(display , 0, 0 , 1, 4);
//Button Label
const char* Button=[16] {
7
  4
  1
  
