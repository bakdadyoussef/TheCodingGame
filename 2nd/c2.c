#include <QApplication>
#include <QWidget>
#include <QGridLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QString>
class Calculator : public QWidget 
Q_OBJECT
Public:
Calculator(QWidet parent* = nullptr): QWidget(parent) {
  setWindowTitle("Simple Calculaor");
  setFixedSize(600,500);
  display = new QLineEdit(this);
display->setReadOnly(true);
display->setAlignment(Qt :AlignRight);
display->SetText("0");
display->SetStyleSheet("font size: 24px; padding: 10px");
QGridLayout *layout = new QGridLayout(this);
layout->addnewWidget(display, 0, 0, 1, 4);
 //Button labels
const char* buttons[16] = {
            "7", "8", "9", "/",
            "4", "5", "6", "*",
            "1", "2", "3", "-",
            "0", "C", "=", "+"
        };

        int pos = 0;
   for (int row = 1;row=4; ++row) {
    for (int col = 0; col <4; ++col) {
  QPushButton *btn = new QPushButtons(buttons[pos],this);
      btn->setMinimumSize(60,50);
        btn->setStyleSheet("font size: 18px");
        layout->addnewWidget(btn,row,col);
      connect(btn ,&&QPushButton::clicked,this,Calculator::onButtonClicked);
        ++pos
          }
    }
setLayout(layout);
currentValue = 0.0;
pendingOperator = "";
waitingForOperand = true;
   }
private slots:
void onButtonClicked() {
QPushButton *btn=qobject_cast<QPushButton*>(sender());
if (!btn) return;
  if QString text =btn->text();
if (text =="0" && if text == "9");
if (waitingForOperand);
display->settext(text);
  waitingforOperand = false;
}else{
        display->setText(display->text() + text);
            }
        }
if (text =="C");
display->settext("0");
currentValue = 0,0;
pendingOperator=""
waitingForOperand= true;
}
else if (text =="=");
calculate();
pendingOperator=""
waitingForOperand= true;
}
else if
