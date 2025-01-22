#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QFile>
#include <flow.h>
#include <QDateTime>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

typedef struct {
  uint time;
  double value;
} RECORD;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private:
    Ui::MainWindow *ui;
    Flow      f;
    FrmResize frs;
    long txcnt;
    long ovrcnt;

    bool RxWait;
    bool m_hold;
    bool m_hold_cpy;
    int WaitCnt;

    int tick_ms;
    int clear_cnt;
    QString rxmsg;

    QString rx_full_buffer;
    QString rx_previous;

    bool chk_rx_full_buffer();

    void initSocket();
    void exitSocket();
    void tx_reset();
    void tx_core(QString s);
    void updateCombo();
    double string2double(QString s);
    uint string2uint(QString s);

    // graph data & functions
    QString ms_LogFile;
    QVector<RECORD> fullLines;

    float m_ymax;
    float m_ymin;
    QVector<double> x;
    QVector<double> y;

    uint m_last_time;
    double search_value(uint time, int *pIndex);
    uint initGraph();
    void updateGraph(uint new_t, double new_y, double raw_y);
    void repaint(int imin, int imax);
    void coreGraph(QCustomPlot *customPlot, QString x_label, QString y_label, QVector<double> & x, QVector<double> & y, double y_min, double y_max, int x_angle);
public slots:
     void readPendingDatagrams();
     void my_timer();
     void slot_hold();
     void slot_reset();

     void slot_ip_change();
     void slot_ip_color(QString s);
     void slot_ymax_plus();
     void slot_ymax_minus();
     void slot_ymin_plus();
     void slot_ymin_minus();

protected:
    // events
    void resizeEvent( QResizeEvent *e );
};
#endif // MAINWINDOW_H
