// Target Tracker - ESP32 (38 pin) + 2 servos + laser dot
// Library needed: "ESP32Servo" (Arduino Library Manager). Nothing else.
// Board: "ESP32 Dev Module"
//
// 1) Flash this file.
// 2) Phone WiFi -> join "TargetTracker" (password 12345678)
// 3) Phone Chrome -> open http://192.168.4.1
//    (camera needs the one-time Chrome flag, see instructions)

#include <WiFi.h>
#include <WebServer.h>
#include <ESP32Servo.h>

#define PAN_PIN   18
#define TILT_PIN  19
#define LASER_PIN 23

const char* AP_SSID = "TargetTracker";
const char* AP_PASS = "12345678";

WebServer server(80);
Servo panServo, tiltServo;

float curPan = 90, curTilt = 90;
float tgtPan = 90, tgtTilt = 90;
const float STEP = 2.5;          // max degrees per tick (smooth, easy on power)
unsigned long lastTick = 0;

const char PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,user-scalable=no">
<title>Target Tracker</title>
<style>
:root{color-scheme:dark;--bg:#0e1116;--fg:#e8edf3;--ac:#ff3b3b;--mut:#8a94a3;--card:#171c24}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--fg);font:14px system-ui,sans-serif;padding:10px}
#box{position:relative;width:100%;aspect-ratio:4/3;background:#000;border-radius:12px;overflow:hidden}
video,canvas#o{position:absolute;inset:0;width:100%;height:100%}
video{object-fit:fill}
.row{display:flex;gap:6px;margin:8px 0}
button{flex:1;padding:12px 4px;border:0;border-radius:10px;background:var(--card);color:var(--fg);font-weight:600}
button.on{background:var(--ac);color:#fff}
.card{background:var(--card);border-radius:12px;padding:8px 12px}
label{display:flex;align-items:center;gap:8px;margin:4px 0;color:var(--mut)}
label span{width:64px}
input[type=range]{flex:1}
#st{color:var(--mut);margin:6px 2px;min-height:18px}
</style></head><body>
<div id="box"><video id="v" playsinline muted autoplay></video><canvas id="o" width="160" height="120"></canvas></div>
<div id="st">Starting camera...</div>
<div class="row">
<button id="bp" class="on">1 Pick</button>
<button id="ba">2 Aim</button>
<button id="bt">3 Track</button>
<button id="bl">Laser</button>
</div>
<div class="card">
<label><span>Pan min</span><input type="range" id="pmin" min="0" max="180" value="40"></label>
<label><span>Pan max</span><input type="range" id="pmax" min="0" max="180" value="140"></label>
<label><span>Tilt min</span><input type="range" id="tmin" min="0" max="180" value="50"></label>
<label><span>Tilt max</span><input type="range" id="tmax" min="0" max="180" value="130"></label>
<label><span>Invert</span><input type="checkbox" id="ip"> pan <input type="checkbox" id="it"> tilt</label>
</div>
<script>
const $=id=>document.getElementById(id);
const v=$('v'),o=$('o'),oc=o.getContext('2d'),st=$('st');
const W=160,H=120,c=document.createElement('canvas');c.width=W;c.height=H;
const cx=c.getContext('2d',{willReadFrequently:true});
let mode='pick',tgt=null,busy=false,lastSend=0,laser=false,lastAim=[.5,.5];
const TOL=18;

if(!navigator.mediaDevices){
  st.textContent='Camera blocked: enable the Chrome flag for http://192.168.4.1 (see instructions)';
}else{
  navigator.mediaDevices.getUserMedia({video:{facingMode:'environment',width:640,height:480},audio:false})
  .then(s=>{v.srcObject=s;st.textContent='Tap the target to pick its color'})
  .catch(e=>st.textContent='Camera error: '+e.message);
}

function hsv(r,g,b){r/=255;g/=255;b/=255;
  const mx=Math.max(r,g,b),mn=Math.min(r,g,b),d=mx-mn;let h=0;
  if(d){if(mx==r)h=((g-b)/d)%6;else if(mx==g)h=(b-r)/d+2;else h=(r-g)/d+4;h*=60;if(h<0)h+=360}
  return [h,mx?d/mx:0,mx]}

function setMode(m){mode=m;
  ['p','a','t'].forEach(k=>$('b'+k).classList.toggle('on',k==m[0]));
  st.textContent={pick:'Tap the target to pick its color',aim:'Tap anywhere to aim the laser there (use for calibration)',track:tgt?'Tracking...':'Pick a color first'}[m];
  if(m=='track'&&!tgt){setMode('pick')}}
$('bp').onclick=()=>setMode('pick');
$('ba').onclick=()=>setMode('aim');
$('bt').onclick=()=>setMode('track');
$('bl').onclick=()=>{laser=!laser;$('bl').classList.toggle('on',laser);fetch('/laser?on='+(laser?1:0))};

$('box').addEventListener('pointerdown',e=>{
  const r=$('box').getBoundingClientRect();
  const fx=(e.clientX-r.left)/r.width,fy=(e.clientY-r.top)/r.height;
  if(mode=='aim'){aim(fx,fy,true)}
  else if(mode=='pick'&&v.videoWidth){
    cx.drawImage(v,0,0,W,H);
    const px=Math.round(fx*W),py=Math.round(fy*H);
    const d=cx.getImageData(Math.max(0,px-2),Math.max(0,py-2),5,5).data;
    let R=0,G=0,B=0,n=d.length/4;
    for(let i=0;i<d.length;i+=4){R+=d[i];G+=d[i+1];B+=d[i+2]}
    const [h,s,val]=hsv(R/n,G/n,B/n);
    tgt={h,s:Math.max(.25,s*.5),v:Math.max(.15,val*.4)};
    st.textContent=s<.25?'Weak color - pick a more saturated target':'Color locked. Press Aim to calibrate, then Track';
  }});

function aim(fx,fy,force){
  const now=performance.now();
  if(!force&&(busy||now-lastSend<50))return;
  if(force&&busy)return;
  lastSend=now;lastAim=[fx,fy];
  let a=+$('pmin').value,b=+$('pmax').value,c2=+$('tmin').value,d=+$('tmax').value;
  if($('ip').checked)[a,b]=[b,a];
  if($('it').checked)[c2,d]=[d,c2];
  const p=Math.round(a+Math.min(1,Math.max(0,fx))*(b-a));
  const t=Math.round(c2+Math.min(1,Math.max(0,fy))*(d-c2));
  busy=true;
  fetch('/aim?p='+p+'&t='+t).catch(()=>{st.textContent='Lost connection to ESP32'}).finally(()=>busy=false);
}
['pmin','pmax','tmin','tmax','ip','it'].forEach(id=>$(id).oninput=()=>{if(mode!='track')aim(lastAim[0],lastAim[1],true)});

function loop(){
  if(v.videoWidth){
    cx.drawImage(v,0,0,W,H);oc.clearRect(0,0,W,H);
    if(tgt){
      const d=cx.getImageData(0,0,W,H).data;let n=0,sx=0,sy=0;
      for(let i=0,p=0;i<d.length;i+=4,p++){
        const [h,s,val]=hsv(d[i],d[i+1],d[i+2]);
        let dh=Math.abs(h-tgt.h);if(dh>180)dh=360-dh;
        if(dh<TOL&&s>tgt.s&&val>tgt.v){n++;sx+=p%W;sy+=(p/W)|0}
      }
      if(n>10){
        const fx=sx/n/W,fy=sy/n/H;
        oc.strokeStyle='#ff3b3b';oc.lineWidth=1.5;
        oc.beginPath();oc.arc(fx*W,fy*H,8,0,6.283);
        oc.moveTo(fx*W-12,fy*H);oc.lineTo(fx*W+12,fy*H);
        oc.moveTo(fx*W,fy*H-12);oc.lineTo(fx*W,fy*H+12);oc.stroke();
        if(mode=='track')aim(fx,fy,false);
      }
    }
  }
  requestAnimationFrame(loop);
}
loop();
</script></body></html>
)rawliteral";

void handleRoot() { server.send_P(200, "text/html", PAGE); }

void handleAim() {
  if (server.hasArg("p")) tgtPan  = constrain(server.arg("p").toInt(), 0, 180);
  if (server.hasArg("t")) tgtTilt = constrain(server.arg("t").toInt(), 0, 180);
  server.send(200, "text/plain", "ok");
}

void handleLaser() {
  digitalWrite(LASER_PIN, server.arg("on") == "1" ? HIGH : LOW);
  server.send(200, "text/plain", "ok");
}

void setup() {
  pinMode(LASER_PIN, OUTPUT);
  digitalWrite(LASER_PIN, LOW);

  panServo.setPeriodHertz(50);
  tiltServo.setPeriodHertz(50);
  panServo.attach(PAN_PIN, 500, 2400);
  tiltServo.attach(TILT_PIN, 500, 2400);
  panServo.write(90);
  tiltServo.write(90);

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);

  server.on("/", handleRoot);
  server.on("/aim", handleAim);
  server.on("/laser", handleLaser);
  server.begin();
}

void loop() {
  server.handleClient();

  if (millis() - lastTick >= 15) {
    lastTick = millis();
    float dp = tgtPan - curPan, dt = tgtTilt - curTilt;
    if (fabs(dp) > 0.5) { curPan  += constrain(dp, -STEP, STEP); panServo.write((int)curPan); }
    if (fabs(dt) > 0.5) { curTilt += constrain(dt, -STEP, STEP); tiltServo.write((int)curTilt); }
  }
}
