#ifndef DISPLAY_CONSOLE_H
#define DISPLAY_CONSOLE_H

#include <Arduino.h>
#include <WiFi.h>
#include <TFT_eSPI.h>

extern const uint8_t Roboto16[]; 

class DisplayConsole {
public:
  DisplayConsole(TFT_eSPI& tft);

  void begin();
  void clear();
  void println(const String& text);
  void print(const String& text);
  void setTelnetClient(WiFiClient* client);
  
  bool isFull();
  void waitForEnter();
  bool checkBreak(); 
  String getInputString(); 

  String getWebBuffer();
  void clearWebBuffer();
  
  void setWebInput(const String& text);
  bool isWaitingForInput();
  
  void continueWeb();
  bool isPaused();

  // --- ДОБАВЛЕНО ДЛЯ СКРОЛЛИНГА ---
  bool _suppressScroll; 
  void setSuppressScroll(bool state) { _suppressScroll = state; }
  // --------------------------------

  void (*onYield)() = nullptr;
  void setYieldCallback(void (*callback)()) { onYield = callback; }

  void startPagination(int totalLines);
  bool isPaginating();
  void nextPage();

private:
  TFT_eSPI& _tft;
  WiFiClient* _telnetClient;

  int _cursorX;
  int _cursorY;
  const int _lineHeight = 20;
  const int _maxY = 260; 

  bool _paginating;
  int _paginateCurrent;
  int _paginateTotal;
  const int _linesPerPage = 12;

  String _webBuffer;
  
  String _webInputBuffer;
  bool _webInputReady;
  
  bool _webContinue; 
  bool _isPaused;    

  void _drawString(const String& text);
};

#endif