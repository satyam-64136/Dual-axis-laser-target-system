// TARGET TRACKER v3 - Auto Laser Turret
// ESP32 (38 pin) + 2 servos + laser dot emitter
// Library needed: "ESP32Servo" (Arduino Library Manager). Board: "ESP32 Dev Module"
//
// 1) Flash.  2) Phone WiFi -> "TargetTracker" (pass 12345678)
// 3) Phone Chrome -> http://192.168.4.1  (one-time camera flag, see instructions)

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

float curPan = 90, curTilt = 90, tgtPan = 90, tgtTilt = 90;
const float STEP = 3.0;
unsigned long lastTick = 0;

const char PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,user-scalable=no">
<title>Auto Turret</title>
<style>
:root{color-scheme:dark;--bg:#0a0d12;--fg:#e8edf3;--ac:#ff3b3b;--ok:#2ee59d;--mut:#7c8696;--card:#151a22}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--fg);font:14px system-ui,sans-serif;padding:10px}
#box{position:relative;width:100%;aspect-ratio:4/3;background:#000;border-radius:12px;overflow:hidden;border:1px solid #243}
video,canvas#o{position:absolute;inset:0;width:100%;height:100%}
video{object-fit:fill}
.row{display:flex;gap:6px;margin:8px 0}
button{flex:1;padding:13px 4px;border:0;border-radius:10px;background:var(--card);color:var(--fg);font-weight:700}
button.on{background:var(--ac);color:#fff}
.card{background:var(--card);border-radius:12px;padding:6px 12px;margin-top:8px}
summary{padding:8px 0;color:var(--mut)}
label{display:flex;align-items:center;gap:8px;margin:4px 0;color:var(--mut)}
label span{width:64px}
input[type=range]{flex:1}
#st{color:var(--mut);margin:6px 2px;min-height:18px}
</style></head><body>
<div id="box"><video id="v" playsinline muted autoplay></video><canvas id="o" width="640" height="480"></canvas></div>
<div id="st">Starting camera...</div>
<div class="row">
<button id="bp" class="on">1 PICK</button>
<button id="ba">2 AIM</button>
<button id="bt">3 AUTO</button>
</div>
<div class="row">
<button id="bl">LASER</button>
</div>
<details class="card"><summary>Calibration</summary>
<label><span>Pan min</span><input type="range" id="pmin" min="0" max="180" value="40"></label>
<label><span>Pan max</span><input type="range" id="pmax" min="0" max="180" value="140"></label>
<label><span>Tilt min</span><input type="range" id="tmin" min="0" max="180" value="50"></label>
<label><span>Tilt max</span><input type="range" id="tmax" min="0" max="180" value="130"></label>
<label><span>Invert</span><input type="checkbox" id="ip"> pan <input type="checkbox" id="it"> tilt</label>
</details>
<script>
const $=id=>document.getElementById(id);
const v=$('v'),o=$('o'),oc=o.getContext('2d'),st=$('st');
const W=160,H=120,S=4,LOCK=1000,TOL=18;
const c=document.createElement('canvas');c.width=W;c.height=H;
const cx=c.getContext('2d',{willReadFrequently:true});
let mode='pick',tgt=null,busy=false,lastSend=0,laser=false;
let lastAim=[.5,.5],sm=null,seenSince=0,lastSeen=0,locks=0,locked=false,ang=[90,90];

/* sound */
let ac=null;
function beep(f,d){try{ac=ac||new (window.AudioContext||window.webkitAudioContext)();
  const o=ac.createOscillator(),g=ac.createGain();o.frequency.value=f;o.connect(g);g.connect(ac.destination);
  g.gain.setValueAtTime(.15,ac.currentTime);g.gain.exponentialRampToValueAtTime(.001,ac.currentTime+d);
  o.start();o.stop(ac.currentTime+d)}catch(e){}}

/* camera */
if(!navigator.mediaDevices){st.textContent='Camera blocked: enable the Chrome flag for http://192.168.4.1'}
else navigator.mediaDevices.getUserMedia({video:{facingMode:'environment',width:640,height:480},audio:false})
 .then(s=>{v.srcObject=s;st.textContent='Tap the target to pick its color'})
 .catch(e=>st.textContent='Camera error: '+e.message);

function hsv(r,g,b){r/=255;g/=255;b/=255;
  const mx=Math.max(r,g,b),mn=Math.min(r,g,b),d=mx-mn;let h=0;
  if(d){if(mx==r)h=((g-b)/d)%6;else if(mx==g)h=(b-r)/d+2;else h=(r-g)/d+4;h*=60;if(h<0)h+=360}
  return [h,mx?d/mx:0,mx]}

function setLaser(on){laser=on;$('bl').classList.toggle('on',on);fetch('/laser?on='+(on?1:0))}
function setMode(m){
  if(m=='auto'&&!tgt){st.textContent='Pick a color first';return}
  mode=m;['p','a','t'].forEach(k=>$('b'+k).classList.toggle('on',k==({pick:'p',aim:'a',auto:'t'})[m]));
  st.textContent={pick:'Tap the target to pick its color',aim:'Tap anywhere to aim. Use Calibration until the laser matches your tap',auto:'AUTO: scan > track > lock'}[m];
  if(m=='auto')setLaser(true);}
$('bp').onclick=()=>setMode('pick');$('ba').onclick=()=>setMode('aim');$('bt').onclick=()=>setMode('auto');
$('bl').onclick=()=>setLaser(!laser);

$('box').addEventListener('pointerdown',e=>{
  const r=$('box').getBoundingClientRect();
  const fx=(e.clientX-r.left)/r.width,fy=(e.clientY-r.top)/r.height;
  if(mode=='aim')aim(fx,fy,true);
  else if(mode=='pick'&&v.videoWidth){
    cx.drawImage(v,0,0,W,H);
    const px=Math.round(fx*W),py=Math.round(fy*H);
    const d=cx.getImageData(Math.max(0,px-2),Math.max(0,py-2),5,5).data;
    let R=0,G=0,B=0,n=d.length/4;
    for(let i=0;i<d.length;i+=4){R+=d[i];G+=d[i+1];B+=d[i+2]}
    const [h,s,val]=hsv(R/n,G/n,B/n);
    tgt={h,s:Math.max(.25,s*.5),v:Math.max(.15,val*.4)};
    beep(990,.1);
    st.textContent=s<.25?'Weak color - pick a brighter, more colorful target':'Color locked. Press AIM to calibrate, then AUTO';
  }});

function aim(fx,fy,force){
  const now=performance.now();
  if(busy||(!force&&now-lastSend<50))return;
  lastSend=now;lastAim=[fx,fy];
  let a=+$('pmin').value,b=+$('pmax').value,c2=+$('tmin').value,d=+$('tmax').value;
  if($('ip').checked)[a,b]=[b,a];
  if($('it').checked)[c2,d]=[d,c2];
  const p=Math.round(a+Math.min(1,Math.max(0,fx))*(b-a));
  const t=Math.round(c2+Math.min(1,Math.max(0,fy))*(d-c2));
  ang=[p,t];busy=true;
  fetch('/aim?p='+p+'&t='+t).catch(()=>{st.textContent='Lost connection to ESP32'}).finally(()=>busy=false);
}
['pmin','pmax','tmin','tmax','ip','it'].forEach(id=>$(id).oninput=()=>{if(mode!='auto')aim(lastAim[0],lastAim[1],true)});

/* HUD */
function bracket(x,y,r,col){oc.strokeStyle=col;oc.lineWidth=3;const l=r*.5;
  [[-1,-1],[1,-1],[1,1],[-1,1]].forEach(([sx,sy])=>{
   oc.beginPath();oc.moveTo(x+sx*r,y+sy*(r-l));oc.lineTo(x+sx*r,y+sy*r);oc.lineTo(x+sx*(r-l),y+sy*r);oc.stroke()})}
function hud(now,prog,scanning){
  oc.font='bold 18px monospace';oc.textBaseline='top';
  oc.fillStyle='#e8edf3';oc.fillText('LOCKS '+locks,12,10);
  if(mode=='auto'){oc.fillStyle=locked?'#ff3b3b':scanning?'#ffcc00':'#2ee59d';
    oc.fillText(locked?'LOCKED':scanning?'SCANNING':'TRACKING '+Math.round(prog*100)+'%',12,34)}
  /* radar */
  const rx=570,ry=70,rr=52;oc.strokeStyle='#2ee59d88';oc.lineWidth=2;
  oc.beginPath();oc.arc(rx,ry,rr,0,6.283);oc.moveTo(rx-rr,ry);oc.lineTo(rx+rr,ry);oc.moveTo(rx,ry-rr);oc.lineTo(rx,ry+rr);oc.stroke();
  oc.beginPath();oc.arc(rx,ry,rr/2,0,6.283);oc.stroke();
  const dx=(lastAim[0]-.5)*2*rr,dy=(lastAim[1]-.5)*2*rr;
  oc.fillStyle=locked?'#ff3b3b':'#2ee59d';oc.beginPath();oc.arc(rx+dx,ry+dy,6,0,6.283);oc.fill();
  oc.strokeStyle='#2ee59d55';oc.beginPath();oc.moveTo(rx,ry);oc.lineTo(rx+dx,ry+dy);oc.stroke();
  oc.fillStyle='#7c8696';oc.font='13px monospace';oc.fillText('P'+ang[0]+' T'+ang[1],rx-36,ry+rr+6);
}

function loop(){
  const now=performance.now();
  oc.clearRect(0,0,640,480);
  let found=false,prog=0,scanning=false;
  if(v.videoWidth){
    cx.drawImage(v,0,0,W,H);
    if(tgt){
      const d=cx.getImageData(0,0,W,H).data;let n=0,sx=0,sy=0;
      for(let i=0,p=0;i<d.length;i+=4,p++){
        const [h,s,val]=hsv(d[i],d[i+1],d[i+2]);
        let dh=Math.abs(h-tgt.h);if(dh>180)dh=360-dh;
        if(dh<TOL&&s>tgt.s&&val>tgt.v){n++;sx+=p%W;sy+=(p/W)|0}
      }
      if(n>10){found=true;const fx=sx/n/W,fy=sy/n/H;
        sm=sm?[sm[0]+(fx-sm[0])*.5,sm[1]+(fy-sm[1])*.5]:[fx,fy];
        if(!seenSince){seenSince=now}lastSeen=now}
    }
  }
  if(!found&&now-lastSeen>500){seenSince=0;sm=null;if(locked){locked=false}}
  if(sm){
    prog=Math.min(1,(now-seenSince)/LOCK);
    const x=sm[0]*640,y=sm[1]*480,r=34+(1-prog)*26;
    bracket(x,y,r,locked?'#ff3b3b':'#2ee59d');
    oc.strokeStyle='#ff3b3b';oc.lineWidth=3;oc.beginPath();oc.arc(x,y,r+10,-1.57,-1.57+6.283*prog);oc.stroke();
  }
  if(mode=='auto'){
    if(sm){
      aim(sm[0],sm[1],false);
      if(prog>=1&&!locked){locked=true;locks++;beep(880,.15)}
    }else if(now-lastSeen>1500){
      scanning=true;
      aim(.5+.5*Math.sin(now/1400),.5+.12*Math.sin(now/3100),false);
    }
  }
  hud(now,prog,scanning);
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
  pinMode(LASER_PIN, OUTPUT); digitalWrite(LASER_PIN, LOW);

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
