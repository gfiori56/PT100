#include "mainwindow.h"
#include "myglobal.h"
#include "ui_mainwindow.h"
#include <QUdpSocket>
#include <QTimer>
#include <QDateTime>

QUdpSocket * udpSocket;
QString curr_cmd;

//#define AUTOMATIC_HOLD

#define ZERO_K   -273.15
#define Y_MAX    40.0
#define Y_MIN    15.0
#define X_ORIENT 90
#define X_NAME       ""
#define Y_NAME       "Temperature"
#define LOGFILE_NAME "Pt100"
#define SAMPLES_NR   1440

#ifdef FAST_MODE
#define SAMPLING_SEC 1
#define T_FORMAT     "yyyy-MM-dd hh:mm:ss"
#else
#define SAMPLING_SEC 60
#define T_FORMAT     "yyyy-MM-dd hh:mm"
#endif

void MainWindow::updateCombo()
{
    ui->cmb_ip->clear();
    for(int i = 0; i < gi_ITEMS; i++)
    {
        ui->cmb_ip->addItem(gs_ITEMS[i]);
    }
    ui->cmb_ip->setCurrentText(gs_ip);
    //ui->cmb_ip->setStyleSheet("background-color: lightgreen;color: black");
    QFont fnt = ui->cmb_ip->font();
    fnt.setItalic(false);
    ui->cmb_ip->setFont(fnt);
    qDebug() << "UpdateCombo() Arduino IP=" << gs_ip;
}

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    setWindowTitle(gs_title + " - libFlow " + f.Version());
    rxmsg.clear();
    rx_previous.clear();

    KeyFile cnf(gs_rdsfile);
    m_ymin = cnf.get_number("Y_MIN", Y_MIN);
    m_ymax = cnf.get_number("Y_MAX", Y_MAX);
    m_hold = cnf.get_number("HOLD", 0);
    m_hold_cpy = !m_hold;

    qDebug() << "Y MAX/MIN/HOLD" << m_ymax << m_ymin << m_hold;

    QString s = LOGFILE_NAME;
    s.append("_" + QString::number(SAMPLING_SEC) + "s.csv");
    ms_LogFile = f.HomeAndFile(s);

    WaitCnt = 0;
    tick_ms = 10;
    clear_cnt = 500;

    ui->txt_1->clear();
    ui->txt_2->clear();
    ui->txt_3->clear();
    ui->txt_4->clear();
    ui->txt_overrun->clear();

    // initialize formresize class
    frs.add(ui->txt_1, FrmResize::CW_TYPE::eTEXT);
    frs.add(ui->txt_2, FrmResize::CW_TYPE::eTEXT);
    frs.add(ui->txt_3, FrmResize::CW_TYPE::eTEXT);
    frs.add(ui->txt_4, FrmResize::CW_TYPE::eTEXT);
    frs.add(ui->bt_hold, FrmResize::CW_TYPE::eBUTTON);
    frs.add(ui->bt_reset, FrmResize::CW_TYPE::eBUTTON);
    frs.add(ui->bt_ymax_plus, FrmResize::CW_TYPE::eBUTTON);
    frs.add(ui->bt_ymax_minus, FrmResize::CW_TYPE::eBUTTON);
    frs.add(ui->bt_ymin_plus, FrmResize::CW_TYPE::eBUTTON);
    frs.add(ui->bt_ymin_minus, FrmResize::CW_TYPE::eBUTTON);
    frs.add(ui->txt_overrun, FrmResize::CW_TYPE::eTEXT);
    //frs.add(ui->lb_overrun, FrmResize::CW_TYPE::eGENERIC);
    frs.add(ui->cmb_ip, FrmResize::CW_TYPE::eGENERIC);
    frs.add(ui->bt_ip, FrmResize::CW_TYPE::eBUTTON);
    frs.add(ui->bt_exit, FrmResize::CW_TYPE::eBUTTON);
    frs.add(ui->customPlot, FrmResize::CW_TYPE::eGENERIC);
    frs.add_form_size(this->width(), this->height(), RDS_FILE, this);

    curr_cmd.clear();
    txcnt = 0;
    ovrcnt = 0;
    RxWait = false;

    ui->bt_reset->setEnabled(m_hold);
    if(m_hold)
        ui->bt_hold->setText("Run");
    else
        ui->bt_hold->setText("Hold");
    rx_full_buffer.clear();
    initSocket();

    ui->cmb_ip->setMaxVisibleItems(MAX_ITEMS);
    updateCombo();
    slot_ip_change();

    QTimer *timer = new QTimer(this);
    connect(timer, SIGNAL(timeout()), this, SLOT(my_timer()));
    connect(ui->bt_hold, SIGNAL(clicked()), this, SLOT(slot_hold()));
    connect(ui->bt_reset, SIGNAL(clicked()), this, SLOT(slot_reset()));
    connect(ui->bt_ip, SIGNAL(clicked()), this, SLOT(slot_ip_change()));
    connect(ui->bt_exit, SIGNAL(clicked()), this, SLOT(close()));
    connect(ui->bt_ymax_plus, SIGNAL(clicked()), this, SLOT(slot_ymax_plus()));
    connect(ui->bt_ymax_minus, SIGNAL(clicked()), this, SLOT(slot_ymax_minus()));
    connect(ui->bt_ymin_plus, SIGNAL(clicked()), this, SLOT(slot_ymin_plus()));
    connect(ui->bt_ymin_minus, SIGNAL(clicked()), this, SLOT(slot_ymin_minus()));
    QObject::connect(ui->cmb_ip, SIGNAL(editTextChanged(QString)), this, SLOT(slot_ip_color(QString)));

    m_last_time = initGraph();
    qDebug() << "my_timer START";
    timer->start(tick_ms);
}
MainWindow::~MainWindow()
{
    frs.rdsSave();
    exitSocket();
    delete ui;
}
// -------------------------------------------------------
//          UDP functions
// -------------------------------------------------------
void MainWindow::initSocket()
{
      udpSocket = new QUdpSocket(this);
      connect(udpSocket, SIGNAL(readyRead()), this, SLOT(readPendingDatagrams()));
}
void MainWindow::exitSocket()
{
      udpSocket->close();
      delete udpSocket;
}

void MainWindow::tx_core(QString s)
{
static int id = 0;
    id++;
    QString sid = QString::number(id, 16);
    if(sid.length() < 4) sid = QString(4 - sid.length(), '0') + sid;
    s = "<" + sid + ">" + s + "\r\n";
    qDebug() << "TX:" << s;
    QByteArray msg = s.toUtf8();
    udpSocket->writeDatagram(msg, QHostAddress(gs_ip), gi_port);
    if(id > 0x7fff) id = 0;
}


// ---------------------------------------------------------
//                  TIMER slot
// ---------------------------------------------------------
void MainWindow::my_timer() {

    if(m_hold_cpy != m_hold)
    {
        qDebug() << "------CHANGE HOLD STATUS:" << m_hold;
         m_hold_cpy = m_hold;
    }

    if(RxWait == true) {
        WaitCnt++;
        if(WaitCnt > clear_cnt) {
            qDebug() << "Response timeout: hold";
            RxWait= false;
            ovrcnt++;
            ui->txt_overrun->setText(QString::number(ovrcnt) + " error(s)");
#ifdef AUTOMATIC_HOLD
            m_hold = true;
            ui->bt_reset->setEnabled(m_hold);
            ui->bt_hold->setText("Run");
#endif
        }
        return;
    }
    if(m_hold)
    {
        return;
    }
    // check time to request data
    uint next_time = m_last_time + SAMPLING_SEC;

    QDateTime dt = QDateTime::currentDateTime();
    uint curr_time  = dt.toTime_t();

    if(curr_time < next_time) return;

    curr_cmd.clear();
    if(curr_time < next_time + SAMPLING_SEC)
    {
        WaitCnt = 0;
        RxWait = true;
        curr_cmd = "t";
        txcnt++;
        QString s = curr_cmd + QString::number(txcnt);
        tx_core(s);
        qDebug() << "RUN tx data, time request:" << QDateTime::fromTime_t(next_time) << "current time:" << dt;

    } else {
        m_last_time += SAMPLING_SEC;
        updateGraph(m_last_time, ZERO_K, ZERO_K);
        qDebug() << "RUN invalid data, time request:" << QDateTime::fromTime_t(m_last_time) << "current time:" << dt;
    }
}

//--------------------------------------------------
// helper functions
//--------------------------------------------------
double MainWindow::string2double(QString s)
{
    QString estrai = s.trimmed();
    int n = estrai.indexOf(" ");
    if(n > 0) estrai = estrai.left(n);
    return estrai.toDouble();
}
uint MainWindow::string2uint(QString s)
{
    QString estrai = s.trimmed();
    int n = estrai.indexOf(" ");
    if(n > 0) estrai = estrai.left(n);
    return estrai.toUInt();
}
double MainWindow::search_value(uint time, int *pIndex)
{
    double value = ZERO_K;
    int i_start = *pIndex;
    for(int i = i_start; i < fullLines.count(); i++)
    {
        if(time == fullLines[i].time)
        {
            value = fullLines[i].value;
            *pIndex = i;
            break;
        }
    }
    return value;
}
void MainWindow::tx_reset()
{
    curr_cmd = "X";
    tx_core(curr_cmd);
}
// ---------------------------------------------------------------------
//                  connected events (slots)
// ---------------------------------------------------------------------
// readPendingDatagrams() helper: rx buffer management
bool MainWindow::chk_rx_full_buffer()
{
  int i, count;
  int i_end = -1;
  int i_start1 = -1;
  int i_start2 = -1;

  rx_full_buffer.append(rxmsg);
  count = rx_full_buffer.count();

  for(i = count - 1; i >= 0; i--)
  {
    if(i_end < 0)
    {
      if(rx_full_buffer[i] == QChar('\n'))  i_end = i;
    } else if(i_start2 < 0)
    {
      if(rx_full_buffer[i] == QChar('>')) i_start2 = i;
    } else if(i_start1 < 0)
    {
      if(rx_full_buffer[i] == QChar('<'))  i_start1 = i;
    }
    if(rx_full_buffer[i] < QChar(' ')) rx_full_buffer[i] = QChar(' ');
  }
  if(i_end >= 0 && (i_start2 < 0  || i_start1 < 0))
  {
    int j = 0;
    for(i = i_end + 1; i < count; i++)
    {
        rx_full_buffer[j++] = rx_full_buffer[i];
    }
    rx_full_buffer[j] = 0;
    qDebug() << "---intital message lost";
    return false;
  }
  if(i_end < 0 || i_start2 < 0  || i_start1 < 0)
  {
    qDebug() << "---message not completed";
    return false;
  }

  QString s = rx_full_buffer.mid(i_start1, i_end - i_start1 + 1);
  rx_full_buffer = rx_full_buffer.mid(i_start2 + 1, i_end - i_start2 - 1);

  //qDebug()  << "s=" << s;
  //qDebug()  << "rx_full_buffer=" << rx_full_buffer;

  if(rx_previous == s)
  {
    qDebug() << "---double message";
    // avoid double messages
    rx_full_buffer.clear();
    return false;
  }
  rx_previous = s;
  return true;
}
void MainWindow::readPendingDatagrams() {
      while (udpSocket->hasPendingDatagrams()) {
          QByteArray datagram;
          datagram.resize(udpSocket->pendingDatagramSize());
          QHostAddress sender;
          quint16 senderPort;
          udpSocket->readDatagram(datagram.data(), datagram.size(), &sender, &senderPort);
          rxmsg = QString(datagram);
          if(chk_rx_full_buffer() == true) {
              QStringList my_list = rx_full_buffer.split(QLatin1Char(';'));
              if(curr_cmd == 't')
              {
                  m_last_time += SAMPLING_SEC;
                  double value = ZERO_K;
                  double raw = ZERO_K;
                  ui->txt_4->clear();
                  if(my_list.count() > 2) ui->txt_1->setText(my_list[2]); else ui->txt_1->clear();
                  if(my_list.count() > 3) ui->txt_2->setText(my_list[3]); else ui->txt_2->clear();
                  QDateTime dt = QDateTime::fromTime_t(m_last_time);
                  ui->txt_3->setText(dt.toString(T_FORMAT));
                  if(my_list.count() > 4)
                  {
                      ui->txt_4->setText(my_list[4]);
                      value = string2double(my_list[4]);
                      raw = string2double(my_list[3]);
                  }
                  updateGraph(m_last_time, value, raw);
              } else {
                if(my_list.count() > 0) ui->txt_1->setText(my_list[0]); else ui->txt_1->clear();
                if(my_list.count() > 1) ui->txt_2->setText(my_list[1]); else ui->txt_2->clear();
                if(my_list.count() > 2) ui->txt_3->setText(my_list[2]); else ui->txt_3->clear();
                if(my_list.count() > 3) ui->txt_4->setText(my_list[3]); else ui->txt_4->clear();
              }
              RxWait = false;
              rx_full_buffer.clear();
              qDebug() << "my_list[0] =" << my_list[0];
          }
      }
}
void MainWindow::slot_hold()
{
    m_hold = !m_hold;
    ui->bt_reset->setEnabled(m_hold);
    if(m_hold)
        ui->bt_hold->setText("Run");
    else
        ui->bt_hold->setText("Hold");

    KeyFile cnf(gs_rdsfile);
    cnf.put_number("HOLD", (int)m_hold);
    cnf.save();

}
void MainWindow::slot_reset()
{
    txcnt = 0;
    ovrcnt = 0;
    ui->txt_overrun->clear();
    tx_reset();
}
void MainWindow::slot_ip_change()
{
    QString s = ui->cmb_ip->currentText();
    s = s.trimmed();
    ui->cmb_ip->setCurrentText(s);
    if(s.length() > 0 && isValidIP(s) == false) {
        //ui->cmb_ip->setStyleSheet("background-color: red;color: black");
        QFont fnt = ui->cmb_ip->font();
        fnt.setItalic(true);
        ui->cmb_ip->setFont(fnt);
        return;
    }
    //ui->cmb_ip->setStyleSheet("background-color: lightgreen;color: black");
    QFont fnt = ui->cmb_ip->font();
    fnt.setItalic(false);
    ui->cmb_ip->setFont(fnt);

    if(s.length() > 0) {
        gs_ip = s;
        qDebug() << "slot_ip_change() Arduino IP=" << gs_ip;
        bool new_item = true;
        for(int i = 0; i < gi_ITEMS; i++) {
            if(gs_ITEMS[i] == gs_ip) {
                new_item = false;
                break;
            }
        }
        if(new_item == true)
        {
            if(gi_ITEMS == MAX_ITEMS) {
                // re-order
                gi_ITEMS--;
                for(int i = FIX_ITEMS; i < gi_ITEMS; i++) {
                    gs_ITEMS[i] = gs_ITEMS[i+1];
                }
            }
            gs_ITEMS[gi_ITEMS] = gs_ip;
            gi_ITEMS++;
        }
    } else {
        // delete last item
        if(gi_ITEMS > FIX_ITEMS) gi_ITEMS--;
        gs_ip = gs_ITEMS[0];
    }
    updateCombo();
    // write RDS
    qDebug() << "UPDATE" << gs_rdsfile;
    KeyFile cnf(gs_rdsfile);
    cnf.put_number("NUMBER", gi_ITEMS);
    cnf.put_string("TEXT", gs_ip);
    for(int i = 0; i < gi_ITEMS; i++)
    {
        cnf.put_string(QString::number(i), gs_ITEMS[i]);
    }
    cnf.save();
}
void MainWindow::slot_ip_color(QString s)
{
   qDebug() << "IP CHANGED (YELLOW):" << s;
   //ui->cmb_ip->setStyleSheet("background-color: lightyellow;color: black");
   QFont fnt = ui->cmb_ip->font();
   fnt.setItalic(true);
   ui->cmb_ip->setFont(fnt);
}

void MainWindow::slot_ymax_plus()
{
    repaint(0, 1);
}
void MainWindow::slot_ymax_minus()
{
    repaint(0, -1);
}
void MainWindow::slot_ymin_plus()
{
    repaint(1, 0);
}
void MainWindow::slot_ymin_minus()
{
    repaint(-1, 0);
}

// ----------------------------------------------------
//                 system events
// ----------------------------------------------------
void MainWindow::resizeEvent( QResizeEvent *e )
{
    frs.exec(this->width(), this->height());
    QWidget::resizeEvent(e);
}
// ----------------------------------------------------
//               GRAPH
// ----------------------------------------------------

void MainWindow::repaint(int imin, int imax)
{
    if(imin != 0)
    {
        m_ymin += imin;
        KeyFile cnf(gs_rdsfile);
        cnf.put_number("Y_MIN", (int) m_ymin);
        cnf.save();
    }
    if(imax != 0)
    {
        m_ymax += imax;
        KeyFile cnf(gs_rdsfile);
        cnf.put_number("Y_MAX", (int) m_ymax);
        cnf.save();
    }
    qDebug() << "REPAINT MAX/MIN" << m_ymax << m_ymin;
    coreGraph(ui->customPlot, X_NAME, Y_NAME, x, y, m_ymin, m_ymax, X_ORIENT);
}

uint MainWindow::initGraph()
{
    QVector <uint> t;
    x.clear();
    y.clear();
    t.clear();
    QDateTime dt = QDateTime::currentDateTime();
    uint tt_curr = dt.toTime_t();
    uint tt_tmp = tt_curr / (uint) SAMPLING_SEC;
    tt_tmp *= (uint) SAMPLING_SEC;
    uint tt_start = tt_tmp - SAMPLING_SEC * (SAMPLES_NR -1);

    uint tt = tt_start;
    for(int i = 0; i < SAMPLES_NR; i++)
    {
        y.append(ZERO_K);
        x.append((double) tt);
        t.append(tt);
        tt += SAMPLING_SEC;
    }
    uint tt_last = 0;
    // read logfile
    QFile logfile(ms_LogFile);
    QString s;
    QByteArray ba;
    fullLines.clear();
    if(f.FileExists(ms_LogFile) == false) {
       s = "DateTime;time_t;rawT;T\r\n";
       if(logfile.open(QIODevice::WriteOnly) == true)
       {
         ba = s.toLatin1();
         logfile.write(ba);
       }
       logfile.close();
    } else {
       if(logfile.open(QIODevice::ReadOnly) == true)
       {
            logfile.readLine(KEYVAL_LENLINE);  //skip header
            do {
              ba = logfile.readLine(KEYVAL_LENLINE);
              if(ba.count() > 0)
              {
                 s = QString(ba);
                 s.remove(QChar('\r'));
                 s.remove(QChar('\n'));

                 QStringList my_line = s.split(QLatin1Char(';'));
                 RECORD rec;
                 if (my_line.count() > 3) {
                    rec.time = string2uint(my_line[1]);
                    rec.time /= (uint) SAMPLING_SEC;
                    rec.time *= (uint) SAMPLING_SEC;
                    rec.value = string2double(my_line[3]);
                    if(rec.time >= tt_start)
                    {
                        fullLines.append(rec);
                        if(tt_last < rec.time)
                        {
                            tt_last = rec.time;
                            ui->txt_1->setText("---");
                            ui->txt_2->setText("---");
                            ui->txt_3->setText(my_line[0]);
                            ui->txt_4->setText(my_line[3] + " ºC");
                        }
                     }
                 }
               }
            } while(!logfile.atEnd());
           qDebug() << "elementi nella lista storica che interessano:" <<  fullLines.count();
           if(tt_last >= tt_start)
           {
                // reinit data
                int index = 0;
                for(int i = 0; i < SAMPLES_NR; i++)
                {
                   y[i] = search_value(t[i], &index);
                   if(y[i] != ZERO_K)
                   {
                       //qDebug() << "fill !!!" << i << t[i] << QDateTime::fromTime_t(t[i]) << y[i];
                   }
                }
           }
       }
       logfile.close();
    }
    //qDebug() << "tt_last  (da file) " << tt_last << QDateTime::fromTime_t(tt_last);
    //qDebug() << "tt_start (da clock)" << tt_start << QDateTime::fromTime_t(tt_start);
    //qDebug() << "tt_curr  (da clock)" << tt_curr << QDateTime::fromTime_t(tt_curr);

    repaint(0, 0);
    if(tt_last > 0) return tt_last;
    return tt_curr;
}
void MainWindow::updateGraph(uint new_t, double new_y, double raw_y)
{
    int i;
    for(i = 0; i < SAMPLES_NR - 1; i++)
    {
          y[i] = y[i + 1];
          x[i] = x[i + 1];
    }
    i = SAMPLES_NR - 1;
    x[i] = (double) new_t;
    y[i] = new_y;


    // write logfile
    QFile logfile(ms_LogFile);
    //DateTime;time_t;rawT;T\r\n
    QDateTime dt = QDateTime::fromTime_t(new_t);
    QString s = dt.toString(T_FORMAT) + ";"
              + QString::number(new_t) + ";"
              + QString::number(raw_y) + ";"
              + QString::number(new_y) + "\r\n";
    qDebug() << s;
    QByteArray ba;
    if(logfile.open(QIODevice::Append) == true)
    {
     ba = s.toLatin1();
     logfile.write(ba);
    }
    logfile.close();
    repaint(0, 0);
}

void MainWindow::coreGraph(QCustomPlot *customPlot, QString x_label, QString y_label, QVector<double> & x, QVector<double> & y, double y_min, double y_max, int x_angle)
{
    // ---clear----
    QVector<double> ticks;
    QVector<QString> labels;

    ticks.clear();
    labels.clear();

    customPlot->xAxis->setTickVector(ticks);
    customPlot->xAxis->setTickVectorLabels(labels);

    customPlot->clearGraphs();
    customPlot->clearItems();
    customPlot->clearPlottables();
    customPlot->clearFocus();
    customPlot->clearMask();
    // ------------------

    customPlot->xAxis->setTickLabelType(QCPAxis::ltDateTime);
    customPlot->xAxis->setDateTimeFormat(T_FORMAT);
    customPlot->xAxis->setTickLabelRotation(x_angle);

    customPlot->addGraph();
    customPlot->graph(0)->setData(x, y);

    customPlot->xAxis->setLabel(x_label);
    customPlot->yAxis->setLabel(y_label);

    customPlot->xAxis->setRange(x[0], x[SAMPLES_NR-1]);
    customPlot->yAxis->setRange(y_min, y_max);
    customPlot->replot();
}
