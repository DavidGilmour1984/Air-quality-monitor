#include <WiFi.h>
#include <HardwareSerial.h>
#include <TinyGPSPlus.h>
#include <Wire.h>
#include "RTClib.h"

HardwareSerial UART2(2);
TinyGPSPlus gps;
RTC_DS3231 rtc;

#define RX_PIN 26
#define TX_PIN 27

uint8_t frame[9];

float pm25 = 0;

double latitude = 0;
double longitude = 0;
double altitude = 0;

String currentTime = "--";

WiFiServer server(80);

const char* ssid = "ESP32 Access Point";
const char* password = "";

unsigned long lastUpdate = 0;

String csvData = "Time,Latitude,Longitude,Altitude,PM2.5\n";

void setup()
{
Serial.begin(9600);

UART2.begin(9600, SERIAL_8N1, RX_PIN, TX_PIN);

Wire.begin();

if(!rtc.begin())
{
Serial.println("RTC not found");
}

WiFi.softAP(ssid, password);
server.begin();

Serial.println("Access Point Started");
Serial.println(WiFi.softAPIP());
}

void updateRTCfromGPS()
{
if(gps.time.isValid() && gps.date.isValid())
{
int year = gps.date.year();

if(year > 2023 && year < 2035)
{
rtc.adjust(DateTime(
gps.date.year(),
gps.date.month(),
gps.date.day(),
gps.time.hour(),
gps.time.minute(),
gps.time.second()
));
}
}
}

void readZPH()
{
if (UART2.available())
{
if (UART2.read() == 0xFF)
{
frame[0] = 0xFF;

for (int i = 1; i < 9; i++)
{
while (!UART2.available());
frame[i] = UART2.read();
}

float lowPulseRate = frame[3] + frame[4] / 100.0;

pm25 = lowPulseRate;
}
}
}

void readGPS()
{
while (UART2.available())
{
char c = UART2.read();
gps.encode(c);
}

if(gps.location.isValid())
{
latitude = gps.location.lat();
longitude = gps.location.lng();
}

if(gps.altitude.isValid())
{
altitude = gps.altitude.meters();
}

updateRTCfromGPS();
}

void updateTime()
{
DateTime now = rtc.now();

char buf[25];

sprintf(buf,"%04d-%02d-%02d %02d:%02d:%02d",
now.year(),
now.month(),
now.day(),
now.hour(),
now.minute(),
now.second());

currentTime = String(buf);
}

String webpage()
{
String page = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta name="viewport" content="width=device-width, initial-scale=1">

<style>

body{
font-family: Helvetica, Arial, sans-serif;
background:#f5f7fb;
padding:30px;
}

.card{
background:white;
padding:30px;
border-radius:14px;
box-shadow:0 6px 20px rgba(0,0,0,0.08);
max-width:1100px;
margin:auto;
}

table{
width:100%;
border-collapse:collapse;
font-size:22px;
}

th,td{
padding:10px;
border-bottom:1px solid #ddd;
text-align:left;
}

th{
background:#f0f3f9;
}

button{
font-size:24px;
padding:10px 20px;
margin:10px;
background:#2563eb;
color:white;
border:none;
border-radius:8px;
cursor:pointer;
}

button:hover{
background:#1e40af;
}

</style>

<script>

function update(){

fetch("/data")
.then(r=>r.json())
.then(d=>{

let table = document.getElementById("log")

let row = table.insertRow(1)

row.insertCell(0).innerText = d.time
row.insertCell(1).innerText = d.lat
row.insertCell(2).innerText = d.lon
row.insertCell(3).innerText = d.alt
row.insertCell(4).innerText = d.pm

})

}

function clearData(){

fetch("/clear")

let table=document.getElementById("log")

while(table.rows.length>1){
table.deleteRow(1)
}

}

function downloadCSV(){
window.location="/csv"
}

setInterval(update,1000)

</script>
</head>

<body>

<div class="card">

<h2>Live Sensor Log</h2>

<button onclick="clearData()">Clear Data</button>
<button onclick="downloadCSV()">Download CSV</button>

<table id="log">

<tr>
<th>Time</th>
<th>Latitude</th>
<th>Longitude</th>
<th>Altitude</th>
<th>PM2.5</th>
</tr>

</table>

</div>

</body>
</html>
)rawliteral";

return page;
}

void handleClient()
{
WiFiClient client = server.available();

if (!client) return;

String request = client.readStringUntil('\r');
client.flush();

if(request.indexOf("/data") != -1)
{

csvData += currentTime + ",";
csvData += String(latitude,6) + ",";
csvData += String(longitude,6) + ",";
csvData += String(altitude,1) + ",";
csvData += String(pm25,2) + "\n";

String json = "{";

json += "\"time\":\""+currentTime+"\",";
json += "\"lat\":\""+String(latitude,6)+"\",";
json += "\"lon\":\""+String(longitude,6)+"\",";
json += "\"alt\":\""+String(altitude,1)+"\",";
json += "\"pm\":\""+String(pm25,2)+"\"";

json += "}";

client.println("HTTP/1.1 200 OK");
client.println("Content-Type: application/json");
client.println("Connection: close");
client.println();
client.println(json);

return;
}

if(request.indexOf("/clear") != -1)
{
csvData = "Time,Latitude,Longitude,Altitude,PM2.5\n";

client.println("HTTP/1.1 200 OK");
client.println("Content-Type: text/plain");
client.println("Connection: close");
client.println();
client.println("CLEARED");

return;
}

if(request.indexOf("/csv") != -1)
{
client.println("HTTP/1.1 200 OK");
client.println("Content-Type: text/csv");
client.println("Content-Disposition: attachment; filename=data.csv");
client.println("Connection: close");
client.println();
client.println(csvData);

return;
}

client.println("HTTP/1.1 200 OK");
client.println("Content-Type: text/html");
client.println("Connection: close");
client.println();
client.println(webpage());
}

void loop()
{

readGPS();

readZPH();

if(millis()-lastUpdate > 1000)
{
updateTime();
lastUpdate = millis();
}

handleClient();

}
