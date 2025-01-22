#include <QString>
extern QString gs_title;
extern QString gs_ip;
extern qint16 gi_port;
extern QString gs_rdsfile;
#define FIX_ITEMS 2
#define MAX_ITEMS 5
extern QString gs_ITEMS[MAX_ITEMS];
extern int gi_ITEMS;
extern bool isValidIP(QString ip);
extern bool itExists(QString s, int n);
#define RDS_FILE "arPT100.rds"
