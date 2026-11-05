#include <WiFi.h>
#include <WebServer.h>
#include <TFT_eSPI.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "display_console.h"
#include "basic_engine.h"
#include "Roboto16.h"
#include "Roboto28.h"
#include "secrets.h"

extern const uint8_t Roboto16[];
extern const uint8_t Roboto28[];

const char* ssid     = "WIFI_SSID";
const char* password = "WIFI_PASSWORD";

TFT_eSPI tft = TFT_eSPI();
WiFiServer telnetServer(23);
WiFiClient telnetClient;
WebServer webServer(80);

TaskHandle_t Task1; 

DisplayConsole console(tft);
BasicEngine basic(console, tft);

String pendingCommand = "";
volatile bool globalStopRequested = false; 
String globalPrompt = "";
bool firstLineLoaded = true;
bool isRunning = false;

const char* htmlPage = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>ESP32 BASIC Web Console</title>
  <style>
    body { font-family: monospace; background: #1e1e1e; color: #00ff00; margin: 0; padding: 10px; display: flex; flex-direction: column; height: 100vh; box-sizing: border-box; }
    #output { flex-grow: 1; background: #000; border: 1px solid #333; padding: 10px; overflow-y: scroll; white-space: pre-wrap; margin-bottom: 10px; font-size: 14px; }
    #input-area { display: flex; gap: 5px; }
    #cmdInput { flex-grow: 1; padding: 10px; font-size: 16px; font-family: monospace; background: #222; color: #00ff00; border: 1px solid #555; }
    button { padding: 10px; font-size: 14px; background: #333; color: #fff; border: none; border-radius: 4px; cursor: pointer; }
    button:active { background: #555; }
    #shortcuts { display: flex; flex-wrap: wrap; gap: 5px; margin-top: 10px; }
    .key-btn { background: #555500; padding: 8px 12px; }
    .sys-btn { background: #005500; padding: 8px 12px; }
    
    #modal { display: none; position: fixed; top: 0; left: 0; width: 100%; height: 100%; background: rgba(0,0,0,0.8); justify-content: center; align-items: center; z-index: 100; }
    #modal-content { background: #222; padding: 20px; border-radius: 8px; width: 90%; max-width: 600px; display: flex; flex-direction: column; gap: 10px; }
    #programText { width: 100%; height: 200px; font-family: monospace; background: #111; color: #0f0; border: 1px solid #555; padding: 10px; resize: vertical; }
    #fileInput { color: #fff; }

    #inputModal { display: none; position: fixed; top: 0; left: 0; width: 100%; height: 100%; background: rgba(0,0,0,0.8); justify-content: center; align-items: center; z-index: 200; }
    #inputModal-content { background: #222; padding: 20px; border-radius: 8px; width: 80%; max-width: 400px; display: flex; flex-direction: column; gap: 10px; }
    #inputField { width: 100%; padding: 10px; font-size: 16px; font-family: monospace; background: #111; color: #0f0; border: 1px solid #555; }
  </style>
</head>
<body>
  <div id="output">Проверка связи с ESP32...</div>
  
  <div id="input-area">
    <input type="text" id="cmdInput" placeholder="Введите команду и нажмите Enter..." autocomplete="off" autofocus>
    <button onclick="sendCommand()">➤</button>
    <button onclick="openModal()" style="background:#005500;">📂 Загрузить</button>
  </div>

  <div id="shortcuts">
    <button class="key-btn" onclick="insertText('PRINT ')">PRINT</button>
    <button class="key-btn" onclick="insertText('INPUT ')">INPUT</button>
    <button class="key-btn" onclick="insertText('THEN ')">THEN</button>
    <button class="key-btn" onclick="insertText('GOTO ')">GOTO</button>
    <button class="key-btn" onclick="insertText('LINE ')">LINE</button>
    <button class="key-btn" onclick="insertText('RECT ')">RECT</button>
    <button class="key-btn" onclick="insertText('CIRCLE ')" style="background:#444400;">CIRCLE</button>
    <button class="key-btn" onclick="insertText('TEXT ')" style="background:#444400;">TEXT</button>
    <button class="key-btn" onclick="insertText('COLOR ')" style="background:#444400;">COLOR</button>
  </div>

  <div id="shortcuts" style="margin-top: 5px;">
    <button class="sys-btn" onclick="sendSpecial('CLS')">CLS</button>
    <button class="sys-btn" onclick="sendSpecial('LIST')">LIST</button>
    <button class="sys-btn" onclick="sendSpecial('RUN')">RUN</button>
    <button class="sys-btn" onclick="sendSpecial('HELP')">HELP</button>
    <button class="sys-btn" onclick="sendSpecial('OPS')">OPS</button>
    <button class="sys-btn" onclick="sendSpecial('NEW')">NEW</button>
    <button class="sys-btn" onclick="sendSpecial('N')">N (Далее)</button>
    <button class="sys-btn" id="continueBtn" onclick="sendContinue()" style="background:#005500; display:none;">▶ Продолжить</button>
    <button style="background:#550000;" onclick="sendSpecial('Ctrl+Break')">STOP</button>
  </div>

  <div id="modal">
    <div id="modal-content">
      <h3 style="margin:0;">Загрузка программы</h3>
      <input type="file" id="fileInput" accept=".txt,.bas">
      <p>или вставьте код вручную:</p>
      <textarea id="programText" placeholder="10 PRINT &quot;HELLO&quot;&#10;20 GOTO 10"></textarea>
      <div style="display:flex; gap:10px; justify-content:flex-end;">
        <button onclick="closeModal()">Отмена</button>
        <button onclick="uploadProgram()" style="background:#005500;">Загрузить в ESP32</button>
      </div>
    </div>
  </div>

  <div id="inputModal">
    <div id="inputModal-content">
      <h3 id="inputTitle" style="margin:0;">INPUT</h3>
      <p>Введите значение:</p>
      <input type="text" id="inputField" autocomplete="off">
      <div style="display:flex; gap:10px; justify-content:flex-end;">
        <button onclick="sendWebInput()" style="background:#005500;">Отправить</button>
        <button onclick="cancelWebInput()" style="background:#550000;">STOP</button>
      </div>
    </div>
  </div>

  <script>
    const outputDiv = document.getElementById('output');
    const inputField = document.getElementById('cmdInput');
    const inputModal = document.getElementById('inputModal');
    const webInputField = document.getElementById('inputField');
    let lastInputState = false;

    setTimeout(() => {
      fetch('/getlog')
        .then(response => response.text())
        .then(data => {
          outputDiv.innerText = "✅ Подключено к ESP32\n---\n";
          outputDiv.style.color = "#00ff00";
        })
        .catch(err => {
          outputDiv.innerText = "❌ Нет связи с ESP32\nПроверьте IP и Wi-Fi.";
          outputDiv.style.color = "#ff0000";
        });
    }, 500);

    inputField.addEventListener('keypress', function (e) {
      if (e.key === 'Enter') sendCommand();
    });

    webInputField.addEventListener('keypress', function (e) {
      if (e.key === 'Enter') sendWebInput();
    });

    function sendCommand() {
      const cmd = inputField.value;
      inputField.value = ''; 
      fetch('/cmd?text=' + encodeURIComponent(cmd))
        .then(response => response.text())
        .then(data => updateOutput(data));
    }

    function sendSpecial(cmd) {
      fetch('/special?cmd=' + encodeURIComponent(cmd))
        .then(response => response.text())
        .then(data => updateOutput(data));
    }

    function sendContinue() {
      fetch('/continue')
        .then(() => {
          document.getElementById('continueBtn').style.display = 'none';
        });
    }

    function sendWebInput() {
      const val = webInputField.value;
      webInputField.value = '';
      inputModal.style.display = 'none';
      lastInputState = false;
      
      fetch('/input?text=' + encodeURIComponent(val))
        .then(response => response.text());
    }

    function cancelWebInput() {
      inputModal.style.display = 'none';
      lastInputState = false;
      fetch('/input?text=BREAK');
    }

    function insertText(text) {
      inputField.value += text;
      inputField.focus();
    }

    function updateOutput(newText) {
      if (newText && newText.length > 0) {
        outputDiv.innerText += newText;
        outputDiv.scrollTop = outputDiv.scrollHeight;
      }
    }

    function openModal() { document.getElementById('modal').style.display = 'flex'; }
    function closeModal() { document.getElementById('modal').style.display = 'none'; }

    document.getElementById('fileInput').addEventListener('change', function(event) {
      const file = event.target.files[0];
      if (!file) return;
      const reader = new FileReader();
      reader.onload = function(e) {
        document.getElementById('programText').value = e.target.result;
      };
      reader.readAsText(file);
    });

    function uploadProgram() {
      const text = document.getElementById('programText').value;
      const lines = text.split('\n');
      closeModal();
      outputDiv.innerText += "\n--- Загрузка программы ---\n";
      
      let i = 0;
      function sendNext() {
        if (i >= lines.length) {
          setTimeout(() => {
            outputDiv.innerText += "--- Загрузка завершена ---\n";
            outputDiv.scrollTop = outputDiv.scrollHeight;
          }, 500);
          return;
        }
        
        let line = lines[i].trim();
        if (line.length > 0) {
          fetch('/cmd?text=' + encodeURIComponent(line))
            .then(response => response.text())
            .then(data => {
              i++;
              setTimeout(sendNext, 150); 
            })
            .catch(err => {
              outputDiv.innerText += "❌ Ошибка загрузки\n";
            });
        } else {
          i++;
          sendNext();
        }
      }
      sendNext();
    }

    setInterval(() => {
      fetch('/getlog')
        .then(response => response.text())
        .then(data => {
          if (data.length > 0) {
            updateOutput(data);
          }
        });

      fetch('/check_input')
        .then(response => response.text())
        .then(inputData => {
          if (inputData !== "NO") {
            if (!lastInputState) {
              document.getElementById('inputTitle').innerText = inputData;
              inputModal.style.display = 'flex';
              webInputField.focus();
              lastInputState = true;
            }
          } else {
            lastInputState = false;
            inputModal.style.display = 'none'; 
          }
        });

      fetch('/pause_status')
        .then(response => response.text())
        .then(data => {
          if (data === "PAUSE") {
            document.getElementById('continueBtn').style.display = 'inline-block';
          } else {
            document.getElementById('continueBtn').style.display = 'none';
          }
        });
    }, 500);
  </script>
</body>
</html>
)rawliteral";

void webServerTask(void * pvParameters) {
  for(;;) {
    webServer.handleClient();
    vTaskDelay(1 / portTICK_PERIOD_MS); 
  }
}

void setup() {
  Serial.begin(115200);
  
  tft.init();
  tft.setRotation(1);
  tft.setSwapBytes(true);
  console.begin();
  
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.loadFont(Roboto16);
  tft.drawString("Wi-Fi connecting...", 10, 10);
  tft.unloadFont();

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  int attempt = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    attempt++;
    tft.fillRect(10, 40, 20, 20, (attempt % 2) ? TFT_BLACK : TFT_YELLOW);
  }
  tft.fillRect(10, 40, 20, 20, TFT_BLACK);

  telnetServer.begin();
  telnetServer.setNoDelay(true);

  // Баннер
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.loadFont(Roboto28); 
  tft.drawString("Компилятор BASIC", 20, 15);
  tft.unloadFont();

  tft.loadFont(Roboto16);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.drawString("Wi-Fi OK", 20, 55);
  
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("IP-адрес:", 20, 85);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.drawString(WiFi.localIP().toString(), 120, 85, 4);

  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString("Способы подключения:", 20, 125);
  
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("1. PuTTY (Telnet, порт 23)", 20, 155);
  tft.drawString("2. Браузер (http://IP)", 20, 175);

  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.drawString("HELP - справка", 20, 215);
  tft.drawString("OPS  - операторы", 260, 215);
  tft.unloadFont();

  tft.drawRect(5, 5, 470, 240, TFT_DARKGREY);

  webServer.on("/", []() { webServer.send(200, "text/html", htmlPage); });
  
  webServer.on("/cmd", []() {
    String cmd = webServer.arg("text");
    webServer.send(200, "text/plain", "");
    pendingCommand = cmd;
  });

  webServer.on("/special", []() {
    String cmd = webServer.arg("cmd");
    
    // --- КНОПКА STOP ---
    if (cmd == "Ctrl+Break") {
      // Если программа НЕ запущена, игнорируем STOP
      if (!isRunning) {
        webServer.send(200, "text/plain", "");
        return;
      }
      
      // Если запущена - останавливаем
      basic.requestBreak();       
      console.setWebInput("BREAK");    
      webServer.send(200, "text/plain", "");
      return;                     
    }
    
    // --- ОСТАЛЬНЫЕ КНОПКИ (RUN, CLS и т.д.) ---
    webServer.send(200, "text/plain", "");
    pendingCommand = cmd;
  });

  webServer.on("/input", []() {
    String text = webServer.arg("text");
    console.setWebInput(text);
    webServer.send(200, "text/plain", "");
  });

  webServer.on("/check_input", []() {
    if (console.isWaitingForInput()) {
      webServer.send(200, "text/plain", globalPrompt);
    } else {
      webServer.send(200, "text/plain", "NO");
    }
  });

  webServer.on("/getlog", []() {
    String logData = console.getWebBuffer();
    webServer.send(200, "text/plain", logData);
  });

  // Новые обработчики для паузы
  webServer.on("/continue", []() {
    console.continueWeb();
    webServer.send(200, "text/plain", "");
  });

  webServer.on("/pause_status", []() {
    if (console.isPaused()) {
      webServer.send(200, "text/plain", "PAUSE");
    } else {
      webServer.send(200, "text/plain", "NO");
    }
  });

  webServer.begin();

  xTaskCreatePinnedToCore(
    webServerTask,   
    "WebServerTask", 
    10000,           
    NULL,            
    1,               
    &Task1,          
    0);              
}

void loop() {
  if (pendingCommand.length() > 0) {
    String cmd = pendingCommand;
    pendingCommand = ""; 
    
    // Устанавливаем флаг, если запускаем программу
    if (cmd == "RUN") isRunning = true;
    
    // Сбрасываем флаг шапки, если очищаем программу
    if (cmd == "NEW") firstLineLoaded = true;
    
    // Если шапка еще висит, стираем её при ЛЮБОЙ первой команде (кроме CLS)
    if (firstLineLoaded && cmd != "CLS") {
      console.clear();
      firstLineLoaded = false;
    }
    
    basic.processCommand(cmd);
    
    // Сбрасываем флаг после завершения программы
    if (cmd == "RUN") isRunning = false;
  }

  if (telnetServer.hasClient()) {
    if (!telnetClient || !telnetClient.connected()) {
      if (telnetClient) telnetClient.stop();
      telnetClient = telnetServer.available();
      console.setTelnetClient(&telnetClient);
      telnetClient.println("=== ESP32 BASIC ===");
      telnetClient.println("Type 'help' for commands.");
    } else {
      telnetServer.available().stop();
    }
  }

  if (telnetClient && telnetClient.connected() && telnetClient.available()) {
    String command = "";
    while (telnetClient.available()) {
      uint8_t b = telnetClient.read();
      if (b == 0xFF) { if (telnetClient.available()) telnetClient.read(); if (telnetClient.available()) telnetClient.read(); continue; }
      if (b == '\n' || b == '\r') { if (command.length() > 0) break; else continue; }
      if (b >= 32 && b != 127) command += (char)b;
    }
    command.trim();
    if (command.length() > 0) {
      // Если шапка еще висит, стираем её при ЛЮБОЙ первой команде (кроме CLS)
      if (firstLineLoaded && command != "CLS") {
        console.clear();
        firstLineLoaded = false;
      }
      
      basic.processCommand(command);
    }
  }
}
