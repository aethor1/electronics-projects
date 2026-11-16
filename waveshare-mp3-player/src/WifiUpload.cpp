#include "WifiUpload.h"
#include <SD_MMC.h>
#include <WebServer.h>
#include <WiFi.h>
#include <uri/UriGlob.h>
#include <string>
#include "PlayerConfig.h"
#include "TransferCore.h"

namespace upload {
namespace {

const char kPage[] PROGMEM = R"HTML(<!doctype html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>Addy's MP3 Player</title><style>
:root{--bg:#101923;--card:#18242f;--ink:#e7ebf2;--muted:#a4b5c8;--accent:#72bedd;--ok:#7fd49b;--bad:#ff8a80}
*{box-sizing:border-box}body{font:16px -apple-system,system-ui,sans-serif;background:var(--bg);color:var(--ink);
padding:20px 16px 40px;max-width:520px;margin:auto}h1{font-size:26px;margin:4px 0 2px}p{color:var(--muted);margin:0 0 18px}
.pick{display:block;background:var(--accent);color:#06222e;text-align:center;font-weight:700;font-size:18px;padding:16px;border-radius:14px}
input{display:none}.card{background:var(--card);border-radius:14px;padding:6px 14px;margin-top:14px}
.row{padding:10px 0;border-bottom:1px solid #263544}.row:last-child{border:0}
.name{overflow:hidden;text-overflow:ellipsis;white-space:nowrap}.st{font-size:13px;color:var(--muted)}
.bar{height:4px;background:#263544;border-radius:2px;margin-top:6px}.bar i{display:block;height:100%;width:0;background:var(--accent);border-radius:2px}
.ok{color:var(--ok)}.bad{color:var(--bad)}button{width:100%;margin-top:18px;padding:14px;border-radius:14px;border:0;
background:#294766;color:var(--ink);font-size:16px;font-weight:600}small{display:block;color:var(--muted);margin-top:12px;font-size:13px}
</style></head><body><h1>Add music</h1><p id="free">Addy's MP3 Player</p>
<label class="pick" for="f">Choose MP3s</label><input id="f" type="file" accept=".mp3,audio/mpeg" multiple>
<div class="card" id="q" hidden></div><button id="done">Done &ndash; turn off Wi-Fi</button>
<small>Keep this page open and the phone unlocked until every song says Added.</small>
<script>
const q=document.getElementById('q'),free=document.getElementById('free');let queue=[],busy=false;
function status(){fetch('/api/status').then(r=>r.json()).then(s=>{free.textContent=(s.freeBytes/1e9).toFixed(2)+' GB free on the player'}).catch(()=>{})}
status();
document.getElementById('f').onchange=e=>{for(const file of e.target.files){if(!/\.mp3$/i.test(file.name))continue;
const row=document.createElement('div');row.className='row';
row.innerHTML='<div class="name"></div><div class="st">Waiting&hellip;</div><div class="bar"><i></i></div>';
row.firstChild.textContent=file.name;q.appendChild(row);q.hidden=false;queue.push({file,row,tries:0})}
e.target.value='';next()};
function next(){if(busy||!queue.length)return;busy=true;send(queue.shift())}
function send(job){const st=job.row.children[1],bar=job.row.children[2].firstChild,x=new XMLHttpRequest();
x.open('PUT','/api/upload');x.setRequestHeader('X-File-Name',encodeURIComponent(job.file.name));
x.upload.onprogress=e=>{if(e.lengthComputable){const p=Math.round(e.loaded*100/e.total);bar.style.width=p+'%';st.textContent='Sending '+p+'%'}};
x.onload=()=>{let r={};try{r=JSON.parse(x.responseText)}catch(e){}
if(x.status==200){bar.style.width='100%';st.className='st ok';st.textContent=r.duplicate?'Already on the player':'Added'}
else{st.className='st bad';st.textContent=r.error||('Failed ('+x.status+')')}finish()};
x.onerror=x.ontimeout=()=>{if(++job.tries<2){st.textContent='Retrying...';setTimeout(()=>send(job),1500)}
else{st.className='st bad';st.textContent='Failed - check the Wi-Fi and try again';finish()}};x.send(job.file)}
function finish(){busy=false;status();next()}
document.getElementById('done').onclick=()=>{if((busy||queue.length)&&!confirm('Songs are still sending. Stop anyway?'))return;
fetch('/api/done',{method:'POST'}).finally(()=>{document.body.innerHTML='<h1>All set</h1><p>The player turned its Wi-Fi off. You can rejoin your normal Wi-Fi now.</p>'})};
</script></body></html>)HTML";

SemaphoreHandle_t lock = nullptr;
Status shared;
WebServer *server = nullptr;
TaskHandle_t serverTask = nullptr;
volatile bool stopRequested = false;
uint64_t freeBytes = 0;

struct Upload {
    File file;
    String tempPath;
    std::string name, savedName;
    uint8_t *buffer = nullptr;
    size_t buffered = 0;
    uint32_t expected = 0, received = 0;
    int code = 0;  // 0 = fine so far, else HTTP error to report
    String message;
    bool duplicate = false;
} up;

template <class F> void update(F change) {
    xSemaphoreTake(lock, portMAX_DELAY);
    change(shared);
    xSemaphoreGive(lock);
}
void touch() { update([](Status &s) { s.lastActivityMs = millis(); }); }

String urlDecode(const String &in) {
    String out;
    out.reserve(in.length());
    for (unsigned i = 0; i < in.length(); ++i) {
        const char c = in[i];
        if (c == '%' && i + 2 < in.length() && isxdigit(in[i + 1]) && isxdigit(in[i + 2])) {
            const char hex[3] = {in[i + 1], in[i + 2], 0};
            out += char(strtol(hex, nullptr, 16));
            i += 2;
        } else out += c;
    }
    return out;
}

void sendJson(int code, const String &body) {
    server->sendHeader("Cache-Control", "no-store");
    server->send(code, "application/json", body);
}

void handleStatus() {
    touch();
    char json[160];
    snprintf(json, sizeof(json), "{\"name\":\"%s\",\"freeBytes\":%llu}",
             player::jsonEscape(player::kWifiName).c_str(), (unsigned long long)freeBytes);
    sendJson(200, json);
}

// Top-level names in /Music, so senders can skip songs that are already there.
void handleFiles() {
    touch();
    server->sendHeader("Cache-Control", "no-store");
    server->setContentLength(CONTENT_LENGTH_UNKNOWN);
    server->send(200, "application/json", "");
    server->sendContent("{\"files\":[");
    File dir = SD_MMC.open(player::kMusicFolder);
    bool first = true;
    if (dir && dir.isDirectory()) {
        File f;
        for (unsigned count = 0; count < 4000 && (f = dir.openNextFile());) {
            String name = f.name();
            name = name.substring(name.lastIndexOf('/') + 1);
            const bool directory = f.isDirectory();
            f.close();
            if (directory || name.startsWith(".")) continue;
            String item = first ? "\"" : ",\"";
            item += player::jsonEscape(name.c_str()).c_str();
            item += "\"";
            server->sendContent(item);
            first = false;
            ++count;
        }
    }
    dir.close();
    server->sendContent("]}");
    server->sendContent("");  // end of chunked response
}

void handleDone() {
    touch();
    sendJson(200, "{\"ok\":true}");
    update([](Status &s) { s.doneRequested = true; });
}

void resetUpload() {
    if (up.file) up.file.close();
    up.tempPath = "";
    up.name.clear();
    up.savedName.clear();
    up.buffered = 0;
    up.expected = up.received = 0;
    up.code = 0;
    up.message = "";
    up.duplicate = false;
}

void failUpload(int code, const char *message) {
    if (!up.code) { up.code = code; up.message = message; }
    if (up.file) up.file.close();
    if (up.tempPath.length()) SD_MMC.remove(up.tempPath);
    up.tempPath = "";
}

bool flushBuffer() {
    if (!up.buffered) return true;
    const size_t written = up.file.write(up.buffer, up.buffered);
    const bool ok = written == up.buffered;
    up.buffered = 0;
    return ok;
}

void finishUpload() {
    if (!flushBuffer()) return failUpload(507, "Writing to the card failed. Is it full?");
    up.file.close();
    if (up.received != up.expected) return failUpload(400, "The upload was incomplete. Try again.");
    for (unsigned attempt = 1; attempt < 100; ++attempt) {
        const std::string candidate = player::numberedName(up.name, attempt);
        const String target = String(player::kMusicFolder) + "/" + candidate.c_str();
        if (!SD_MMC.exists(target)) {
            if (!SD_MMC.rename(up.tempPath, target)) return failUpload(500, "Could not finish saving the song.");
            up.tempPath = "";
            up.savedName = candidate;
            freeBytes = freeBytes > up.expected ? freeBytes - up.expected : 0;
            return;
        }
        File existing = SD_MMC.open(target);
        const size_t size = existing ? existing.size() : 0;
        existing.close();
        if (size == up.expected) {  // same name, same size: already there
            SD_MMC.remove(up.tempPath);
            up.tempPath = "";
            up.savedName = candidate;
            up.duplicate = true;
            return;
        }
    }
    failUpload(409, "Too many songs with this name.");
}

// Raw (non-multipart) PUT body: the file itself, name in X-File-Name (URL-encoded).
// Streams through a 32 KB buffer into a temp file, then renames; never overwrites.
void handleRaw() {
    HTTPRaw &raw = server->raw();
    if (raw.status == RAW_START) {
        resetUpload();
        touch();
        const std::string original = urlDecode(server->header("X-File-Name")).c_str();
        up.expected = uint32_t(server->header("Content-Length").toInt());
        up.name = player::sanitizeUploadName(original);
        update([&](Status &s) {
            s.receiving = true;
            s.bytesExpected = up.expected;
            s.bytesReceived = 0;
            strlcpy(s.currentFile, up.name.c_str(), sizeof(s.currentFile));
        });
        if (!player::hasMp3Extension(original)) return failUpload(415, "Only MP3 files can be added.");
        if (!up.expected) return failUpload(411, "That file is empty.");
        if (uint64_t(up.expected) + player::kUploadSpareBytes > freeBytes)
            return failUpload(507, "The player's card is full.");
        if (!SD_MMC.exists(player::kMusicFolder) && !SD_MMC.mkdir(player::kMusicFolder))
            return failUpload(500, "Could not create the Music folder.");
        if (!up.buffer) up.buffer = static_cast<uint8_t *>(ps_malloc(player::kUploadWriteBuffer));
        if (!up.buffer) up.buffer = static_cast<uint8_t *>(malloc(player::kUploadWriteBuffer));
        if (!up.buffer) return failUpload(500, "The player is out of memory.");
        up.tempPath = String(player::kMusicFolder) + "/.upload-" + String(millis()) + ".tmp";
        up.file = SD_MMC.open(up.tempPath, FILE_WRITE);
        if (!up.file) return failUpload(500, "The card is not writable.");
    } else if (raw.status == RAW_WRITE) {
        up.received += raw.currentSize;
        update([&](Status &s) { s.lastActivityMs = millis(); s.bytesReceived = up.received; });
        if (up.code) return;  // keep draining the body so the error reply can be sent
        const uint8_t *data = raw.buf;
        size_t remaining = raw.currentSize;
        while (remaining) {
            const size_t room = player::kUploadWriteBuffer - up.buffered;
            const size_t n = remaining < room ? remaining : room;
            memcpy(up.buffer + up.buffered, data, n);
            up.buffered += n; data += n; remaining -= n;
            if (up.buffered == player::kUploadWriteBuffer && !flushBuffer())
                return failUpload(507, "Writing to the card failed. Is it full?");
        }
    } else if (raw.status == RAW_END) {
        if (!up.code) finishUpload();
    } else if (raw.status == RAW_ABORTED) {
        failUpload(408, "Upload interrupted.");
        update([](Status &s) {
            s.receiving = false;
            s.currentFile[0] = 0;
            strlcpy(s.lastMessage, "Upload interrupted; try again.", sizeof(s.lastMessage));
        });
        resetUpload();
    }
}

void handleUploadDone() {
    touch();
    String body;
    const int code = up.code ? up.code : 200;
    String message;
    if (up.code) {
        message = up.message;
        body = String("{\"ok\":false,\"error\":\"") + player::jsonEscape(up.message.c_str()).c_str() + "\"}";
    } else {
        message = String(up.duplicate ? "Already on player: " : "Added: ") + up.savedName.c_str();
        body = String("{\"ok\":true,\"saved\":\"") + player::jsonEscape(up.savedName).c_str() +
               "\",\"duplicate\":" + (up.duplicate ? "true" : "false") + "}";
    }
    const bool added = !up.code && !up.duplicate;
    update([&](Status &s) {
        s.receiving = false;
        s.currentFile[0] = 0;
        if (added) ++s.filesSaved;
        strlcpy(s.lastMessage, message.c_str(), sizeof(s.lastMessage));
    });
    sendJson(code, body);
    resetUpload();
}

void removeStaleTempFiles() {
    File dir = SD_MMC.open(player::kMusicFolder);
    if (!dir || !dir.isDirectory()) return;
    String stale[16];
    unsigned count = 0;
    File f;
    while (count < 16 && (f = dir.openNextFile())) {
        String name = f.name();
        name = name.substring(name.lastIndexOf('/') + 1);
        f.close();
        if (name.startsWith(".upload-") && name.endsWith(".tmp"))
            stale[count++] = String(player::kMusicFolder) + "/" + name;
    }
    dir.close();
    for (unsigned i = 0; i < count; ++i) SD_MMC.remove(stale[i]);
}

void serverLoop(void *) {
    uint32_t polledAt = 0;
    while (!stopRequested) {
        server->handleClient();
        if (millis() - polledAt >= 500) {
            polledAt = millis();
            const uint8_t stations = WiFi.softAPgetStationNum();
            update([&](Status &s) { s.stations = stations; });
        }
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    server->stop();
    serverTask = nullptr;
    vTaskDelete(nullptr);
}

}  // namespace

bool begin() {
    if (serverTask) return true;
    if (!lock) lock = xSemaphoreCreateMutex();
    if (!lock) return false;
    update([](Status &s) {
        s = Status{};
        s.active = true;
        s.lastActivityMs = millis();
    });
    removeStaleTempFiles();
    const uint64_t total = SD_MMC.totalBytes(), used = SD_MMC.usedBytes();
    freeBytes = total > used ? total - used : 0;

    WiFi.persistent(false);
    if (!WiFi.mode(WIFI_AP)) return false;
    const IPAddress ip(192, 168, 4, 1), mask(255, 255, 255, 0);
    WiFi.softAPConfig(ip, ip, mask);
    // Channel 6, visible, at most a phone + a Mac at once.
    if (!WiFi.softAP(player::kWifiName, player::kWifiPassword, 6, 0, 2)) {
        WiFi.mode(WIFI_OFF);
        return false;
    }
    WiFi.setTxPower(WIFI_POWER_11dBm);  // a desk-range link; less current than the 20 dBm default

    server = new WebServer(80);
    static const char *headers[] = {"X-File-Name", "Content-Length"};
    server->collectHeaders(headers, 2);
    server->on("/", HTTP_GET, [] { touch(); server->send_P(200, "text/html", kPage); });
    server->on("/api/status", HTTP_GET, handleStatus);
    server->on("/api/files", HTTP_GET, handleFiles);
    server->on("/api/upload", HTTP_PUT, handleUploadDone, handleRaw);
    server->on("/api/done", HTTP_POST, handleDone);
    // Catch-all: unknown URLs get 404, and any unexpected POST/PUT body is streamed
    // away instead of WebServer's default of buffering the whole body in RAM.
    server->on(UriGlob("*"), HTTP_ANY, [] { server->send(404, "text/plain", "Not found"); }, [] {});
    server->begin();

    stopRequested = false;
    if (xTaskCreatePinnedToCore(serverLoop, "upload", 8192, nullptr, 1, &serverTask, 1) != pdPASS) {
        serverTask = nullptr;
        end();
        return false;
    }
    return true;
}

void end() {
    stopRequested = true;
    for (int i = 0; serverTask && i < 300; ++i) vTaskDelay(pdMS_TO_TICKS(10));
    if (serverTask) {
        // Stuck in a long upload: dropping the radio makes the socket read fail fast.
        WiFi.softAPdisconnect(true);
        for (int i = 0; serverTask && i < 500; ++i) vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (!serverTask) {
        delete server;
        server = nullptr;
        resetUpload();
        if (up.buffer) { free(up.buffer); up.buffer = nullptr; }
    }
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    if (lock) update([](Status &s) { s.active = false; s.receiving = false; s.stations = 0; });
}

Status status() {
    Status copy;
    if (!lock) return copy;
    xSemaphoreTake(lock, portMAX_DELAY);
    copy = shared;
    xSemaphoreGive(lock);
    return copy;
}

}  // namespace upload
