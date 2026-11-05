#include "display_console.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

DisplayConsole::DisplayConsole(TFT_eSPI& tft) : _tft(tft) {
  _telnetClient = nullptr;
  _cursorX = 10;
  _cursorY = 10;
  _paginating = false;
  _paginateCurrent = 0;
  _paginateTotal = 0;
  _webBuffer = "";
  _webInputBuffer = "";
  _webInputReady = true; 
  _webContinue = false;
  _isPaused = false;
}

void DisplayConsole::begin() {
  _tft.fillScreen(TFT_BLACK);
  _tft.setTextColor(TFT_GREEN, TFT_BLACK);
  _cursorX = 10;
  _cursorY = 10;
  clear();
}

void DisplayConsole::setTelnetClient(WiFiClient* client) {
  _telnetClient = client;
}

void DisplayConsole::clear() {
  _tft.fillRect(0, 0, 480, 320, TFT_BLACK);
  _cursorX = 10;
  _cursorY = 10;
  _paginating = false;
}

void DisplayConsole::println(const String& text) {
    
  _cursorX = 10; 
  _drawString(text); // _drawString сам решит, стирать экран или нет
  _cursorY += _lineHeight;
  
  if (_telnetClient && _telnetClient->connected()) {
    _telnetClient->println(text);
  }

  _webBuffer += text + "\n";
  if (_webBuffer.length() > 5000) {
    _webBuffer = _webBuffer.substring(1000); 
  }
}

void DisplayConsole::_drawString(const String& text) {
  // Автоочистка ТОЛЬКО если мы НЕ в режиме листинга/справки
  if (_cursorY > _maxY && !_suppressScroll) {
    _tft.fillRect(0, 0, 480, 320, TFT_BLACK);
    _cursorY = 10;
  }
  
  _tft.setTextColor(TFT_GREEN, TFT_BLACK);
  _tft.loadFont(Roboto16);
  _tft.drawString(text, _cursorX, _cursorY);
  _tft.unloadFont();
}

void DisplayConsole::print(const String& text) {
  if (_paginating) return;
  _tft.setTextColor(TFT_GREEN, TFT_BLACK);
  _tft.loadFont(Roboto16);
  _tft.drawString(text, _cursorX, _cursorY);
  _cursorX += _tft.textWidth(text);
  _tft.unloadFont();
  if (_telnetClient && _telnetClient->connected()) {
    _telnetClient->print(text);
  }
  _webBuffer += text;
  if (_webBuffer.length() > 5000) {
    _webBuffer = _webBuffer.substring(1000); 
  }
}

bool DisplayConsole::isFull() {
  return (_cursorY > _maxY);
}

void DisplayConsole::waitForEnter() {
  _webContinue = false;
  _isPaused = true;
  
  if (_telnetClient && _telnetClient->connected()) {
    while (_telnetClient->available()) _telnetClient->read();
  }
  
  bool enterPressed = false;
  
  while (!enterPressed) {
    if (_telnetClient && _telnetClient->connected() && _telnetClient->available()) {
      uint8_t b = _telnetClient->read();
      if (b == '\r' || b == '\n') enterPressed = true;
      else if (b == 0xFF) {
        if (_telnetClient->available()) {
          uint8_t cmd = _telnetClient->read();
          if (cmd == 0xF3 || cmd == 0xF2) { _isPaused = false; return; }
          if (cmd == 0xFB || cmd == 0xFC || cmd == 0xFD || cmd == 0xFE) {
            if (_telnetClient->available()) _telnetClient->read();
          }
        }
      }
      else if (b == 3 || b == 27) { _isPaused = false; return; } 
    }
    
    if (_webContinue) {
      enterPressed = true;
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
  _isPaused = false;
}

void DisplayConsole::continueWeb() {
  _webContinue = true;
}

bool DisplayConsole::isPaused() {
  return _isPaused;
}

bool DisplayConsole::checkBreak() {
  if (!_telnetClient || !_telnetClient->connected()) return false;
  bool breakSignal = false;
  while (_telnetClient->available()) {
    uint8_t b = _telnetClient->read();
    if (b == 0xFF) {
      if (_telnetClient->available()) {
        uint8_t cmd = _telnetClient->read();
        if (cmd == 0xF3 || cmd == 0xF2) breakSignal = true;
        else if (cmd == 0xFB || cmd == 0xFC || cmd == 0xFD || cmd == 0xFE) {
          if (_telnetClient->available()) _telnetClient->read();
        }
      }
      continue;
    }
    if (b == 3 || b == 27) breakSignal = true;
  }
  return breakSignal;
}

String DisplayConsole::getInputString() {
  if (_telnetClient && _telnetClient->connected()) {
    while (_telnetClient->available()) _telnetClient->read();
    String input = "";
    bool enterPressed = false;
    _cursorX += 10; 

    while (!enterPressed) {
      if (_telnetClient->available()) {
        uint8_t b = _telnetClient->read();
        if (b == 0xFF) {
          if (_telnetClient->available()) {
            uint8_t cmd = _telnetClient->read();
            if (cmd == 0xF3 || cmd == 0xF2) return "BREAK"; 
            if (cmd == 0xFB || cmd == 0xFC || cmd == 0xFD || cmd == 0xFE) {
              if (_telnetClient->available()) _telnetClient->read();
            }
          }
          continue;
        }
        if (b == '\r' || b == '\n') {
          enterPressed = true;
        } 
        else if (b == 8 || b == 127) {
          if (input.length() > 0) {
            input.remove(input.length() - 1);
            _tft.fillRect(_cursorX - 6, _cursorY, 6, 20, TFT_BLACK); 
            _cursorX -= 6;
          }
        }
        else if (b >= 32 && b != 127) {
          input += (char)b;
          _tft.drawString(String((char)b), _cursorX, _cursorY);
          _cursorX += 6;
          _telnetClient->print((char)b); 
        }
      }
      vTaskDelay(10 / portTICK_PERIOD_MS);
    }
    return input;
  } 
  else {
    _webInputReady = false;
    _webInputBuffer = "";
    
    extern volatile bool globalStopRequested;
    globalStopRequested = false; 
    
    while (!_webInputReady) {
      if (globalStopRequested) {
        globalStopRequested = false;
        return "BREAK";
      }
      vTaskDelay(10 / portTICK_PERIOD_MS);
    }
    return _webInputBuffer;
  }
}

void DisplayConsole::setWebInput(const String& text) {
  _webInputBuffer = text;
  _webInputReady = true;
}

bool DisplayConsole::isWaitingForInput() {
  if (_telnetClient && _telnetClient->connected()) {
    return false; 
  }
  return !_webInputReady;
}

String DisplayConsole::getWebBuffer() {
  String res = _webBuffer;
  _webBuffer = ""; 
  return res;
}

void DisplayConsole::clearWebBuffer() {
  _webBuffer = "";
}

void DisplayConsole::startPagination(int totalLines) {
  _paginating = true;
  _paginateCurrent = 0;
  _paginateTotal = totalLines;
  nextPage();
}

bool DisplayConsole::isPaginating() {
  return _paginating;
}

void DisplayConsole::nextPage() {
  if (!_paginating) return;
  clear();
}