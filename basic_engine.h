#ifndef BASIC_ENGINE_H
#define BASIC_ENGINE_H

#include <Arduino.h>
#include <WiFi.h>
#include <vector>
#include <map>
#include <algorithm>
#include <TFT_eSPI.h>
#include "display_console.h"

struct BasicLine {
  int number;
  String code;
};

struct ForLoopState {
  String varName;
  float targetValue;
  float stepValue;
  int startLineIndex;
};

struct BasicArray {
  int dims[2];
  int numDims;
  std::vector<float> data;
};

class BasicEngine {
public:
  BasicEngine(DisplayConsole& console, TFT_eSPI& tft); 

  void processCommand(const String& command);
  bool isListing();
  void handlePaginationInput(const String& input);
  void reset();
  void requestBreak(); 
  
  void (*onYield)() = nullptr;
  void setYieldCallback(void (*callback)()) { onYield = callback; }

private:
  DisplayConsole& _console;
  TFT_eSPI& _tft;
  int _currentColor;
  
  std::vector<BasicLine> _program;
  std::map<String, float> _variables;
  std::map<String, String> _stringVars;
  std::map<String, BasicArray> _arrays; 
  
  std::vector<ForLoopState> _forStack;
  bool _breakRequested;
  bool _externalStopRequested;

  // --- ФЛАГИ ДЛЯ ПАГИНАЦИИ ---
  bool _isListing;      // Для LIST
  bool _isHelpListing;  // Для OPS
  int _listIndex;       // Индекс для LIST
  int _helpPage;        // Индекс для OPS
  
  const int LINES_PER_PAGE = 12;
  const int MAX_LINES = 150;

  // --- МАССИВ СПРАВКИ ---
  const char* _helpPages[4] = {
    "Операторы (Стр 1/4):\n"
    "  PRINT \"текст\";A - вывод\n"
    "  INPUT \"текст\",A - ввод\n"
    "  LET A = <выр> - присвоить\n"
    "  LET A$ = \"стр\" - строка\n"
    "  DIM A(10)     - массив\n"
    "  IF <усл> THEN <оп> - условие\n"
    "  FOR I=1 TO 10 - цикл\n"
    "  NEXT I        - конец цикла\n"
    "  GOTO <номер>  - переход\n"
    "  REM текст     - комментарий\n"
    "  END           - завершить\n"
    "  CLS           - очистить экран\n"
    "  PAUSE <сек>   - пауза\n"
    "-- Далее -- (N=Далее, Q=Выход)",

    "Операторы (Стр 2/4):\n"
    "  COLOR n       - цвет (0-15)\n"
    "  PLOT x,y      - точка\n"
    "  LINE x1,y1,x2,y2 - линия\n"
    "  RECT x1,y1,x2,y2 - прямоугольник\n"
    "  TEXT x,y,\"str\" - текст\n"
    "  POINT x,y     - цвет пикселя\n"
    "  PAINT x,y     - заливка\n"
    "  CIRCLE x,y,r  - круг\n"
    "  CIRCLE x,y,r,a1,a2 - дуга\n"
    "    a1,a2 - углы в радианах\n"
    "  CIRCLE x,y,r,a1,a2,asp,ax - эллипс\n"
    "    asp - сжатие (0.1-1.0)\n"
    "    ax  - ось: 1=Y, 2=X\n"
    "-- Далее -- (N=Далее, Q=Выход)",

    "Операторы (Стр 3/4):\n"
    "Математика: + - * / ^ MOD ( )\n"
    "  RND(N) - случайное число\n"
    "  INT(X) - целая часть\n"
    "  ABS(X) - модуль\n"
    "  SQR(X) - квадратный корень\n"
    "  SIN(X) - синус\n"
    "  COS(X) - косинус\n"
    "-- Далее -- (N=Далее, Q=Выход)",

    "Операторы (Стр 4/4):\n"
    "Строки: \"текст\", A$ + B$\n"
    "  LEN(A$) - длина строки\n"
    "  MID$(A$, start, len) - часть\n"
    "  ASC(A$) - код символа\n"
    "  VAL(A$) - строка в число\n"
    "  CHR$(N) - число в символ\n"
    "--- Конец списка ---"
  };
  // -----------------------

  void addLine(int num, const String& code);
  void deleteLine(int num);
  void listProgram(int startIdx);
  void runProgram();
  void printHelp();
  void printOps();
  void printHelpPage(int pageIndex); // <--- ОБЪЯВЛЕНИЕ ФУНКЦИИ
  void executePrint(const String& args);
  void checkBreak();
  String formatNum(float val);
  uint16_t getColor(int num);

  int findMatchingNext(int startIdx);
  void floodFill(int x, int y, uint16_t newColor);

  float evaluateExpression(String expr);
  float parseTerm(String& str);
  float parsePower(String& str);
  String extractParens(String& str);
  void skipSpaces(String& str);

  String evaluateStringExpr(String expr);
};

#endif