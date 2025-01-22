#include "mainwindow.h"
#include "myglobal.h"

#include <QApplication>

QString gs_title;
QString gs_ip;
qint16 gi_port;
QString gs_rdsfile;

QString gs_ITEMS[MAX_ITEMS];
int gi_ITEMS;

bool isValidIP(QString ip) {
    //qDebug() << "IP=" << ip;
    ip = ip.trimmed();
    int l = ip.length();
    if(l < 7) return false;
    QList <QString> items = ip.split(".");
    if(items.count() != 4) return false;
    bool ok;
    for(int i = 0; i < 4; i++) {
        ok = false;
        ushort n = items[i].toUShort(&ok);
        if(ok == false) return false;
        if(n > 255) return false;
        //qDebug() << n;
    }
    return true;
}
bool itExists(QString s, int n) {
    if(n > MAX_ITEMS) n = MAX_ITEMS;
    for(int i = 0; i < n; i++) {
        if(s == gs_ITEMS[i])
            return true;
    }
    return false;
}

int main(int argc, char *argv[])
{
    Flow flib;
    gi_port = 2390;
    gs_title = "Pt100 acquisition from Arduino";
    gs_rdsfile = flib.HomeAndFile(RDS_FILE);

    qDebug() << "READ" << gs_rdsfile;
    KeyFile cnf(gs_rdsfile);
    int n = cnf.get_number("NUMBER", 0);
    gs_ip = cnf.get_string("TEXT", "");
    if(n > 0 && isValidIP(gs_ip) == true) {
        gi_ITEMS = n;
        if(gi_ITEMS > MAX_ITEMS) gi_ITEMS = MAX_ITEMS;
        for(int i = 0; i < gi_ITEMS; i++)
        {
            gs_ITEMS[i] = cnf.get_string(QString::number(i), "");
            if(isValidIP(gs_ITEMS[i]) == false) {
                n = 0;
                break;
            }
            if(itExists(gs_ITEMS[i], i) == true) {
                n = 0;
                break;
            }
        }
    }
    if(n == 0) {
        // write default rds file
        qDebug() << "CREATE" << gs_rdsfile;
        gs_ip = "192.168.1.123";
        gi_ITEMS = FIX_ITEMS;
        gs_ITEMS[0] = gs_ip;
        gs_ITEMS[1] = "5.89.219.15";
        cnf.put_number("NUMBER", gi_ITEMS);
        cnf.put_string("TEXT", gs_ip);
        for(int i = 0; i < gi_ITEMS; i++)
        {
            if(gs_ITEMS[i].length() == 0) gs_ITEMS[i] = "x.x.x.x";
            cnf.put_string(QString::number(i), gs_ITEMS[i]);
        }
        cnf.save();
        qDebug() << "created default RDS file";
    }
    QApplication a(argc, argv);
    MainWindow w;
    w.show();
    return a.exec();
}
