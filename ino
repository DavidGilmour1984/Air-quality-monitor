#include <WiFi.h>
#include <HardwareSerial.h>
#include <TinyGPSPlus.h>
#include <Wire.h>
#include "RTClib.h"
#include <Preferences.h>

HardwareSerial ZPH(1);
HardwareSerial GPS(2);

TinyGPSPlus gps;
RTC_DS3231 rtc;
Preferences prefs;

#define ZPH_RX 26
#define ZPH_TX 27

#define GPS_RX 16
#define GPS_TX 17

const char* ssid = "ESP32 Access Point";
const char* password = "";

WiFiServer server(80);

struct LogRow
{
  String gpsTime;
  String rtcTime;
  double lat;
  double lon;
  float pm;
  int voc;
};

LogRow logData[1000];
int logIndex = 0;

uint8_t frame[9];
float pmPulse = 0;
int vocGrade = 0;

double latitude = 0;
double longitude = 0;

String gpsTime = "--";
String rtcTime = "--";

void saveToFlash()
{
  prefs.putUInt("count", logIndex);

  for(int i=0;i<logIndex;i++)
  {
    String key = "row"+String(i);

    String value =
      logData[i].gpsTime + "," +
      logData[i].rtcTime + "," +
      String(logData[i].lat,6) + "," +
      String(logData[i].lon,6) + "," +
      String(logData[i].pm) + "," +
      String(logData[i].voc);

    prefs.putString(key.c_str(), value);
  }
}

void loadFromFlash()
{
  logIndex = prefs.getUInt("count",0);

  for(int i=0;i<logIndex;i++)
  {
    String key="row"+String(i);
    String row=prefs.getString(key.c_str(),"");

    int p1=row.indexOf(',');
    int p2=row.indexOf(',',p1+1);
    int p3=row.indexOf(',',p2+1);
    int p4=row.indexOf(',',p3+1);
    int p5=row.indexOf(',',p4+1);

    logData[i].gpsTime=row.substring(0,p1);
    logData[i].rtcTime=row.substring(p1+1,p2);
    logData[i].lat=row.substring(p2+1,p3).toDouble();
    logData[i].lon=row.substring(p3+1,p4).toDouble();
    logData[i].pm=row.substring(p4+1,p5).toFloat();
    logData[i].voc=row.substring(p5+1).toInt();
  }
}

void readZPH()
{
  if (ZPH.available())
  {
    if (ZPH.read() == 0xFF)
    {
      frame[0] = 0xFF;

      for(int i=1;i<9;i++)
      {
        while(!ZPH.available());
        frame[i] = ZPH.read();
      }

      pmPulse = frame[3] + frame[4]/100.0;
      vocGrade = frame[7];
    }
  }
}

void readGPS()
{
  while(GPS.available())
  {
    gps.encode(GPS.read());
  }

  if(gps.location.isValid())
  {
    latitude = gps.location.lat();
    longitude = gps.location.lng();
  }

  if(gps.time.isValid())
  {
    gpsTime =
      String(gps.time.hour()) + ":" +
      String(gps.time.minute()) + ":" +
      String(gps.time.second());
  }
}

void readRTC()
{
  DateTime now = rtc.now();

  rtcTime =
    String(now.hour()) + ":" +
    String(now.minute()) + ":" +
    String(now.second());
}

void logValues()
{
  if(logIndex >= 999) return;

  for(int i=logIndex;i>0;i--)
  {
    logData[i]=logData[i-1];
  }

  logData[0].gpsTime = gpsTime;
  logData[0].rtcTime = rtcTime;
  logData[0].lat = latitude;
  logData[0].lon = longitude;
  logData[0].pm = pmPulse;
  logData[0].voc = vocGrade;

  logIndex++;

  saveToFlash();
}

void setup()
{
Serial.begin(9600);

Wire.begin(21,22);
rtc.begin();

prefs.begin("logger",false);
loadFromFlash();

ZPH.begin(9600, SERIAL_8N1, ZPH_RX, ZPH_TX);
GPS.begin(9600, SERIAL_8N1, GPS_RX, GPS_TX);

WiFi.softAP(ssid,password);
server.begin();
}

void loop()
{
readZPH();
readGPS();
readRTC();

static unsigned long lastLog=0;

if(millis()-lastLog>30000)
{
  logValues();
  lastLog=millis();
}

WiFiClient client = server.available();

if(client)
{
String req = client.readStringUntil('\r');

if(req.indexOf("/download")>=0)
{
client.println("HTTP/1.1 200 OK");
client.println("Content-Type: text/csv");
client.println("Content-Disposition: attachment; filename=data.csv");
client.println();

client.println("gps_time,rtc_time,latitude,longitude,pm2.5_pulse,voc_grade");

for(int i=0;i<logIndex;i++)
{
client.print(logData[i].gpsTime); client.print(",");
client.print(logData[i].rtcTime); client.print(",");
client.print(logData[i].lat,6); client.print(",");
client.print(logData[i].lon,6); client.print(",");
client.print(logData[i].pm); client.print(",");
client.println(logData[i].voc);
}

client.stop();
return;
}

if(req.indexOf("/clear")>=0)
{
logIndex=0;
prefs.clear();
}

client.println("HTTP/1.1 200 OK");
client.println("Content-type:text/html");
client.println();

client.println("<html><head>");
client.println("<meta name='viewport' content='width=device-width, initial-scale=1'>");

client.println("<style>");
client.println("body{font-family:Helvetica;background:#f5f7fb;padding:20px}");
client.println("table{width:100%;border-collapse:collapse;font-size:22px}");
client.println("th,td{padding:6px;border:1px solid #ddd;text-align:center}");
client.println("button{font-size:22px;padding:10px 20px;margin:10px}");
client.println("</style>");

client.println("<script>");
client.println("function clearData(){fetch('/clear').then(()=>location.reload());}");
client.println("</script>");

client.println("</head><body>");

client.println("<h2>ESP32 Air Quality Logger</h2>");

client.println("<button onclick=\"window.location='/download'\">Download CSV</button>");
client.println("<button onclick=\"clearData()\">Clear Data</button>");

client.println("<table>");
client.println("<tr><th>GPS Time</th><th>RTC Time</th><th>Latitude</th><th>Longitude</th><th>PM2.5 Pulse %</th><th>VOC Grade</th></tr>");

for(int i=0;i<logIndex;i++)
{
client.println("<tr>");
client.println("<td>"+logData[i].gpsTime+"</td>");
client.println("<td>"+logData[i].rtcTime+"</td>");
client.println("<td>"+String(logData[i].lat,6)+"</td>");
client.println("<td>"+String(logData[i].lon,6)+"</td>");
client.println("<td>"+String(logData[i].pm)+"</td>");
client.println("<td>"+String(logData[i].voc)+"</td>");
client.println("</tr>");
}

client.println("</table>");

client.println("</body></html>");

client.stop();
}
}
