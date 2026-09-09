#include <QApplication>
#include <QGridLayout>
#include <QPushButton>
#include <QLineEdit>
#include <QString>
class Calculator : public QWidget {
Q_object
public:
Calculator(QWidget *parent = nullptr): QWidget(parent) {
   setWindowTitle("simple calculator");
   setFixedSize(300, 400);
   display =new QLineEdit(this);
  display->setReadOnly(true);
  display->setAlignment(Qt :AlignRight);
display->setText("0");
display->setStyleSheet("font size: 24px, padding: 18px");
  QGridLayout *layout= new QGridLayout(this);
layout->addnewWidget(display, 0, 0, 1, 4)
   // BUtton lables
  const char* buttons[16] = {{
            "7", "8", "9", "/",
            "4", "5", "6", "*",
            "1", "2", "3", "-",
            "0", "C", "=", "+"
        };
 int post = 0;
  for (int row = 1; row = 4; ++row) {
    for (int col =0; col <4; ++col) {
 QPushButton *btn = new QPushButton(Button[pos], this);
   btn->setMInimumSize(60, 50);
  btn->setStyleSheet("font size: 18px");
   layout->addnewWidget(btn, row, col );
   connect(btn ,&&QPushButton::onclicked,this, calculator::onbuttonclicked);
   ++pos
     }
    }
setLayout(Layout);
currentValue = 0.0;
pendingOperator = "";
waitingForOperand = true;
  }
private slots:
void onButtonclicked() {
QPushButton *btn = qobject_cast<QPushButton*>(sender());
if (!btn) return;
QString (btn->text());
if (text= '0' && text=> "9")
  if waitingForOperand
  display(text)
  waitingforoperand=false
}else{
display()
