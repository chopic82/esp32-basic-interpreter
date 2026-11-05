#include "basic_engine.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

BasicEngine::BasicEngine(DisplayConsole& console, TFT_eSPI& tft) : _console(console), _tft(tft) {
  _isListing = false;
  _listIndex = 0;
  _forStack.clear();
  _breakRequested = false;
  _externalStopRequested = false;
  _currentColor = TFT_GREEN;
}

void BasicEngine::reset() {
  _program.clear();
  _variables.clear();
  _arrays.clear();
  _forStack.clear();
  _isListing = false;
}

void BasicEngine::checkBreak() {
  // Проверяем только Telnet (Ctrl+Break)
  if (_console.checkBreak()) {
    _breakRequested = true;
  }
}

void BasicEngine::requestBreak() {
  _breakRequested = true;
  _externalStopRequested = true;
}

String BasicEngine::formatNum(float val) {
  if (abs(val - round(val)) < 0.0001) {
    return String((int)round(val));
  }
  return String(val, 2);
}

uint16_t BasicEngine::getColor(int num) {
  switch (num) {
    case 0: return TFT_BLACK;
    case 1: return TFT_NAVY;
    case 2: return TFT_DARKGREEN;
    case 3: return TFT_DARKCYAN;
    case 4: return TFT_MAROON;
    case 5: return TFT_PURPLE;
    case 6: return TFT_OLIVE;
    case 7: return TFT_LIGHTGREY;
    case 8: return TFT_DARKGREY;
    case 9: return TFT_BLUE;
    case 10: return TFT_GREEN;
    case 11: return TFT_CYAN;
    case 12: return TFT_RED;
    case 13: return TFT_MAGENTA;
    case 14: return TFT_YELLOW;
    case 15: return TFT_WHITE;
    default: return TFT_WHITE;
  }
}

void BasicEngine::processCommand(const String& command) {
  String cmd = command;
  cmd.trim();
  String upperCmd = cmd;
  upperCmd.toUpperCase();

  if (_isListing || _isHelpListing) {
    handlePaginationInput(cmd);
    return;
  }

  if (cmd.length() == 0) return;

  // Если нажали "N", но мы не в режиме листинга
  if (cmd == "N" || cmd == "n") {
    _console.println("Листать нечего.");
    return;
  }

  if (isDigit(cmd[0])) {
    int spaceIdx = cmd.indexOf(' ');
    int num = 0;
    String code = "";
    if (spaceIdx == -1) {
      num = cmd.toInt();
      code = ""; 
    } else {
      num = cmd.substring(0, spaceIdx).toInt();
      code = cmd.substring(spaceIdx + 1);
    }
    addLine(num, code);
    return;
  }

  if (upperCmd == "HELP") { printHelp(); }
  else if (upperCmd == "OPS") { printOps(); }
  else if (upperCmd == "CLS") { _console.clear(); }
  else if (upperCmd.indexOf("BREAK") != -1 || upperCmd.indexOf("CTRL") != -1) {
    requestBreak();
    _console.println("?Прервано (Break).");
  }
  else if (upperCmd == "LIST") { listProgram(0); }
  else if (upperCmd.startsWith("LIST ")) {
    int startLine = upperCmd.substring(5).toInt();
    int startIdx = 0;
    for (int i = 0; i < _program.size(); i++) {
      if (_program[i].number >= startLine) {
        startIdx = i;
        break;
      }
    }
    listProgram(startIdx);
  }
  else if (upperCmd.startsWith("DEL ")) {
    int num = upperCmd.substring(4).toInt();
    bool found = false;
    for (auto& line : _program) {
      if (line.number == num) { found = true; break; }
    }
    if (found) {
      deleteLine(num);
      _console.println("Строка " + String(num) + " удалена.");
    } else {
      _console.println("Строка " + String(num) + " не найдена.");
    }
  }
  else if (upperCmd == "RUN") { runProgram(); }
  else if (upperCmd == "NEW") {
    _program.clear();
    _variables.clear();
    _arrays.clear();
    _forStack.clear();
    _console.clear();
    _console.println("Программа и переменные очищены.");
  }
  else if (upperCmd.startsWith("DIM ")) {
    String args = cmd.substring(4);
    args.trim();
    int parenIdx = args.indexOf('(');
    if (parenIdx == -1) {
      _console.println("?Синтаксическая ошибка: пропущены скобки в DIM");
      return;
    }
    String name = args.substring(0, parenIdx);
    name.trim();
    name.toUpperCase();
    String dimsStr = extractParens(args);
    dimsStr.trim();
    BasicArray arr;
    arr.numDims = 1;
    int commaIdx = dimsStr.indexOf(',');
    if (commaIdx != -1) {
      String rStr = dimsStr.substring(0, commaIdx); rStr.trim();
      String cStr = dimsStr.substring(commaIdx + 1); cStr.trim();
      int r = (int)evaluateExpression(rStr);
      int c = (int)evaluateExpression(cStr);
      arr.numDims = 2;
      arr.dims[0] = r + 1; 
      arr.dims[1] = c + 1;
      arr.data.resize(arr.dims[0] * arr.dims[1], 0.0);
      _console.println("Создан массив " + name + "(" + String(r) + "," + String(c) + ")");
    } else {
      int size = (int)evaluateExpression(dimsStr);
      arr.dims[0] = size + 1;
      arr.data.resize(arr.dims[0], 0.0);
      _console.println("Создан массив " + name + "(" + String(size) + ")");
    }
    _arrays[name] = arr;
  }
  else if (upperCmd.startsWith("PRINT ") || upperCmd.startsWith("?") || upperCmd.startsWith("LET ")) {
    String line = cmd;
    String upperLine = upperCmd;
    if (upperLine.startsWith("PRINT ")) { executePrint(line.substring(6)); }
    else if (upperLine.startsWith("?")) { executePrint(line.substring(1)); }
    else if (upperLine.startsWith("LET ")) {
       _console.println("?Ошибка: LET работает только внутри программы.");
    }
  }
  else {
    _console.println("?Неизвестная команда: " + cmd);
  }
}

bool BasicEngine::isListing() { return _isListing; }

void BasicEngine::handlePaginationInput(const String& input) {
  String cmd = input;
  cmd.trim();
  cmd.toUpperCase();

  if (cmd == "N" || cmd == "NEXT") {
    if (_isHelpListing) {
      _helpPage++;
      if (_helpPage >= 4) _helpPage = 0;
      printHelpPage(_helpPage);
      
      if (_helpPage == 3) {
        _isHelpListing = false;
        _console.setSuppressScroll(false); // Выключаем после последней страницы
      }
    } 
    else if (_isListing) {
      if (_listIndex < _program.size()) {
        listProgram(_listIndex);
      } else {
        _isListing = false;
        _console.setSuppressScroll(false);
      }
    }
  }
  else if (cmd == "Q" || cmd == "QUIT") {
    _isListing = false;
    _isHelpListing = false;
    _console.setSuppressScroll(false);
    _console.println("Просмотр прерван.");
  }
  else {
    _console.println("Нажмите N (Далее) или Q (Выход).");
  }
}

void BasicEngine::listProgram(int startIdx) {
  if (_program.empty()) {
    _console.println("Программа пуста.");
    _isListing = false;
    return;
  }
  
  _console.setSuppressScroll(true);
  _isListing = true;
  _console.clear();
  _listIndex = startIdx;
  
  int printed = 0;
  while (_listIndex < _program.size() && printed < LINES_PER_PAGE) {
    BasicLine& line = _program[_listIndex];
    _console.println(String(line.number) + " " + line.code);
    _listIndex++;
    printed++;
  }
  
  if (_listIndex < _program.size()) {
    _console.println("-- Далее -- (N=Далее, Q=Выход)");
  } else {
    _console.println("--- Конец программы ---");
    _isListing = false;
    _console.setSuppressScroll(false);
  }
}

void BasicEngine::addLine(int num, const String& code) {
  if (code.length() == 0) {
    bool found = false;
    for (auto& line : _program) {
      if (line.number == num) { found = true; break; }
    }
    if (found) {
      deleteLine(num);
      _console.println("Строка " + String(num) + " удалена.");
    } else {
      _console.println("Строка " + String(num) + " не найдена.");
    }
    return;
  }
  for (auto& line : _program) {
    if (line.number == num) {
      _console.println("?Ошибка: Строка " + String(num) + " уже существует.");
      _console.println("Используйте DEL " + String(num) + " для её удаления.");
      return;
    }
  }
  if (_program.size() >= MAX_LINES) {
    _console.println("?Ошибка: Память заполнена (макс. 150 строк).");
    return;
  }
  _program.push_back({num, code});
  std::sort(_program.begin(), _program.end(),
    [](const BasicLine& a, const BasicLine& b) { return a.number < b.number; });
  _console.println("Строка " + String(num) + " добавлена.");
}

void BasicEngine::deleteLine(int num) {
  _program.erase(std::remove_if(_program.begin(), _program.end(),
    [num](const BasicLine& l) { return l.number == num; }), _program.end());
}

int BasicEngine::findMatchingNext(int startIdx) {
  int depth = 1;
  for (int i = startIdx + 1; i < _program.size(); i++) {
    String code = _program[i].code;
    code.toUpperCase();
    if (code.startsWith("FOR ")) depth++;
    else if (code.startsWith("NEXT")) {
      depth--;
      if (depth == 0) return i;
    }
  }
  return -1;
}

void BasicEngine::floodFill(int x, int y, uint16_t newColor) {
  uint16_t targetColor = _tft.readPixel(x, y);
  if (targetColor == newColor) return;

  std::vector<std::pair<int, int>> stack;
  stack.push_back({x, y});
  int count = 0;
  const int MAX_PIXELS = 15000; 

  while (!stack.empty() && count < MAX_PIXELS) {
    auto pt = stack.back();
    stack.pop_back();
    int cx = pt.first;
    int cy = pt.second;

    if (cx < 0 || cx >= 480 || cy < 0 || cy >= 320) continue;
    if (_tft.readPixel(cx, cy) != targetColor) continue;

    _tft.drawPixel(cx, cy, newColor);
    count++;

    stack.push_back({cx + 1, cy});
    stack.push_back({cx - 1, cy});
    stack.push_back({cx, cy + 1});
    stack.push_back({cx, cy - 1});
  }
}

void BasicEngine::skipSpaces(String& str) {
  while (str.length() > 0 && str[0] == ' ') {
    str.remove(0, 1);
  }
}

String BasicEngine::extractParens(String& str) {
  skipSpaces(str);
  if (str.length() == 0 || str[0] != '(') return "";
  str.remove(0, 1);
  String inner = "";
  int depth = 1;
  while (str.length() > 0) {
    char c = str[0];
    str.remove(0, 1);
    if (c == '(') depth++;
    else if (c == ')') {
      depth--;
      if (depth == 0) break;
    }
    inner += c;
  }
  return inner;
}

float BasicEngine::parsePower(String& str) {
  skipSpaces(str);
  if (str.length() == 0) return 0;

  float val = 0;

  // 1. Скобки
  if (str[0] == '(') {
    String inner = extractParens(str);
    val = evaluateExpression(inner);
  } 
  // 2. Унарный минус
  else if (str[0] == '-') {
    str.remove(0, 1);
    val = -parsePower(str);
  }
  else if (str[0] == '+') {
    str.remove(0, 1);
    val = parsePower(str);
  }
  // 3. Числа, переменные, функции
  else {
    int i = 0;
    while (i < str.length() && (isAlpha(str[i]) || isDigit(str[i]) || str[i] == '.' || str[i] == '_')) {
      i++;
    }
    if (i == 0) return 0;
    
    String token = str.substring(0, i);
    str.remove(0, i);
    String upperToken = token;
    upperToken.toUpperCase();

    // --- ВСТРОЕННЫЕ МАТЕМАТИЧЕСКИЕ ФУНКЦИИ ---
    if (upperToken == "RND") {
      skipSpaces(str);
      if (str.startsWith("(")) {
        String inner = extractParens(str);
        val = random(1, (long)evaluateExpression(inner) + 1);
      } else {
        val = (float)random(0, 10000) / 10000.0;
      }
    }
    else if (upperToken == "INT") {
      skipSpaces(str);
      if (str.startsWith("(")) {
        String inner = extractParens(str);
        val = (float)((int)evaluateExpression(inner));
      }
    }
    else if (upperToken == "ABS") {
      skipSpaces(str);
      if (str.startsWith("(")) {
        String inner = extractParens(str);
        val = abs(evaluateExpression(inner));
      }
    }
    else if (upperToken == "SIN") {
      skipSpaces(str);
      if (str.startsWith("(")) {
        String inner = extractParens(str);
        val = sin(evaluateExpression(inner));
      }
    }
    else if (upperToken == "COS") {
      skipSpaces(str);
      if (str.startsWith("(")) {
        String inner = extractParens(str);
        val = cos(evaluateExpression(inner));
      }
    }
    else if (upperToken == "SQR") {
      skipSpaces(str);
      if (str.startsWith("(")) {
        String inner = extractParens(str);
        float arg = evaluateExpression(inner);
        if (arg >= 0) val = sqrt(arg);
        else val = 0; 
      }
    }
    // --- СТРОКОВЫЕ ФУНКЦИИ (возвращают число) ---
    else if (upperToken == "ASC") {
      skipSpaces(str);
      if (str.startsWith("(")) {
        String inner = extractParens(str);
        String sVal = evaluateStringExpr(inner);
        if (sVal.length() > 0) {
          val = (float)(uint8_t)sVal[0]; // Код первого байта
        } else {
          val = 0;
        }
      }
    }
    else if (upperToken == "LEN") {
      skipSpaces(str);
      if (str.startsWith("(")) {
        String inner = extractParens(str);
        String sVal = evaluateStringExpr(inner);
        
        // Считаем количество символов UTF-8
        int count = 0;
        for (int j = 0; j < sVal.length(); j++) {
          if ((uint8_t)sVal[j] < 0x80 || (uint8_t)sVal[j] >= 0xC0) {
            count++;
          }
        }
        val = (float)count;
      }
    }
    else if (upperToken == "VAL") {
      skipSpaces(str);
      if (str.startsWith("(")) {
        String inner = extractParens(str);
        String sVal = evaluateStringExpr(inner);
        val = sVal.toFloat();
      }
    }
    // --- ПЕРЕМЕННЫЕ И МАССИВЫ ---
    else if (isAlpha(token[0])) {
      skipSpaces(str);
      if (str.startsWith("(")) {
        if (_arrays.find(upperToken) == _arrays.end()) {
          _console.println("?Ошибка: Массив " + upperToken + " не объявлен!");
          return 0;
        }
        BasicArray& arr = _arrays[upperToken];
        String inner = extractParens(str);
        int i = 0;
        int j = 0;
        if (arr.numDims == 1) {
          i = (int)evaluateExpression(inner);
          if (i < 0 || i >= arr.dims[0]) {
             _console.println("?Ошибка: Индекс " + String(i) + " вне границ");
             return 0;
          }
          val = arr.data[i];
        } else {
          int commaIdx = inner.indexOf(',');
          if (commaIdx == -1) {
            _console.println("?Ошибка: Нужны 2 индекса");
            return 0;
          }
          i = (int)evaluateExpression(inner.substring(0, commaIdx));
          j = (int)evaluateExpression(inner.substring(commaIdx + 1));
          if (i < 0 || i >= arr.dims[0] || j < 0 || j >= arr.dims[1]) {
             _console.println("?Ошибка: Индексы вне границ");
             return 0;
          }
          val = arr.data[i * arr.dims[1] + j];
        }
      } 
      else {
        if (_variables.find(upperToken) != _variables.end()) {
          val = _variables[upperToken];
        } else {
          val = 0; 
        }
      }
    } 
    else {
      val = token.toFloat();
    }
  }

  // 4. Возведение в степень
  skipSpaces(str);
  while (str.length() > 0 && str[0] == '^') {
    str.remove(0, 1); 
    float exponent = parsePower(str); 
    val = pow(val, exponent);
    skipSpaces(str);
  }
  return val;
}

float BasicEngine::parseTerm(String& str) {
  float val = parsePower(str); 
  skipSpaces(str);
  while (str.length() > 0) {
    String remaining = str;
    remaining.toUpperCase();
    if (remaining.startsWith("MOD ")) {
      str.remove(0, 4); 
      float nextVal = parsePower(str); 
      if (nextVal != 0) {
        val = (int)val % (int)nextVal; 
      }
      skipSpaces(str);
      continue;
    }
    char op = str[0];
    if (op == '*' || op == '/') {
      str.remove(0, 1);
      float nextVal = parsePower(str);
      if (op == '*') val *= nextVal;
      else {
        if (nextVal == 0) return 0;
        val /= nextVal;
      }
      skipSpaces(str);
    } else {
      break;
    }
  }
  return val;
}

float BasicEngine::evaluateExpression(String expr) {
  float val = parseTerm(expr);
  skipSpaces(expr);
  while (expr.length() > 0) {
    char op = expr[0];
    if (op == '+' || op == '-') {
      expr.remove(0, 1);
      float nextVal = parseTerm(expr);
      if (op == '+') val += nextVal;
      else val -= nextVal;
      skipSpaces(expr);
    } else {
      break;
    }
  }
  return val;
}

void BasicEngine::executePrint(const String& args) {
  String expr = args;
  expr.trim();
  bool noNewline = false;
  if (expr.endsWith(";")) {
    noNewline = true;
    expr = expr.substring(0, expr.length() - 1);
    expr.trim();
  }
  int start = 0;
  int semiIdx = expr.indexOf(';');
  
  // --- ПЕРВОЕ МЕСТО (внутри цикла) ---
  while (semiIdx != -1) {
    String part = expr.substring(start, semiIdx);
    part.trim();
    if (part.startsWith("\"") && part.endsWith("\"")) {
      part = part.substring(1, part.length() - 1);
      _console.print(part);
    } else {
      String upperPart = part;
      upperPart.toUpperCase();
      // Добавили проверку на MID$ и CHR$
      if (upperPart.endsWith("$") || upperPart.startsWith("LEN(") || upperPart.startsWith("MID$(") || upperPart.startsWith("CHR$(")) {
        _console.print(evaluateStringExpr(part));
      } else {
        float val = evaluateExpression(part);
        _console.print(formatNum(val)); 
      }
    }
    start = semiIdx + 1;
    semiIdx = expr.indexOf(';', start);
  }

  // --- ВТОРОЕ МЕСТО (для lastPart) ---
  String lastPart = expr.substring(start);
  lastPart.trim();
  if (lastPart.length() > 0) {
    if (lastPart.startsWith("\"") && lastPart.endsWith("\"")) {
      lastPart = lastPart.substring(1, lastPart.length() - 1);
      _console.print(lastPart);
    } else {
      String upperPart = lastPart;
      upperPart.toUpperCase();
      // Добавили проверку на MID$ и CHR$
      if (upperPart.endsWith("$") || upperPart.startsWith("LEN(") || upperPart.startsWith("MID$(") || upperPart.startsWith("CHR$(")) {
        _console.print(evaluateStringExpr(lastPart));
      } else {
        float val = evaluateExpression(lastPart);
        _console.print(formatNum(val)); 
      }
    }
  }

  if (!noNewline) {
    _console.println(""); 
  }
}

String BasicEngine::evaluateStringExpr(String expr) {
  expr.trim();
  
  // 1. Текст в кавычках
  if (expr.startsWith("\"") && expr.endsWith("\"")) {
    return expr.substring(1, expr.length() - 1);
  }
  
  // 2. Склейка строк через +
  int plusIdx = -1;
  bool inQuotes = false;
  for (int i = expr.length() - 1; i >= 0; i--) {
    if (expr[i] == '"') inQuotes = !inQuotes;
    else if (expr[i] == '+' && !inQuotes) {
      plusIdx = i;
      break;
    }
  }
  if (plusIdx != -1) {
    String left = expr.substring(0, plusIdx);
    String right = expr.substring(plusIdx + 1);
    return evaluateStringExpr(left) + evaluateStringExpr(right);
  }
  
  String upperExpr = expr;
  upperExpr.toUpperCase();
  upperExpr.replace("\r", "");
  upperExpr.replace("\n", "");
  
  // 3. LEN()
  if (upperExpr.startsWith("LEN(") && upperExpr.endsWith(")")) {
    String inner = expr.substring(4, expr.length() - 1);
    String sVal = evaluateStringExpr(inner);
    
    int count = 0;
    for (int i = 0; i < sVal.length(); i++) {
      if ((uint8_t)sVal[i] < 0x80 || (uint8_t)sVal[i] >= 0xC0) {
        count++; 
      }
    }
    return String(count);
  }
  
  // 4. CHR$() - превращает число в символ (с поддержкой кириллицы UTF-8)
  if (upperExpr.startsWith("CHR$(") && upperExpr.endsWith(")")) {
    String inner = expr.substring(5, expr.length() - 1);
    int code = (int)evaluateExpression(inner);
    
    if (code >= 192 && code <= 255) {
      // Таблица для кодов 192-255 (Windows-1251 -> UTF-8)
      // Мы просто храним все 64 символа в одной строке UTF-8
      String table = "АБВГДЕЖЗИЙКЛМНОПРСТУФХЦЧШЩЪЫЬЭЮЯабвгдежзийклмнопрстуфхцчшщъыьэюя";
      
      // Находим нужный символ в таблице
      int index = code - 192;
      if (index >= 0 && index < 64) {
        // Каждая русская буква в UTF-8 занимает 2 байта
        return table.substring(index * 2, index * 2 + 2);
      }
    } 
    else {
      // Для латиницы и цифр
      char buf[2];
      buf[0] = (char)code;
      buf[1] = '\0';
      return String(buf);
    }
    return "";
  }

  // 5. MID$
  if (upperExpr.startsWith("MID$(")) {
    int c1 = expr.indexOf(',');
    int c2 = expr.indexOf(',', c1 + 1);
    if (c1 != -1 && c2 != -1) {
      String sExpr = expr.substring(5, c1);
      sExpr.trim();
      
      String startExpr = expr.substring(c1 + 1, c2);
      String lenExpr = expr.substring(c2 + 1, expr.length() - 1);
      
      startExpr.trim();
      lenExpr.trim();
      
      String sVal = evaluateStringExpr(sExpr);
      int start = (int)evaluateExpression(startExpr) - 1; 
      int len = (int)evaluateExpression(lenExpr);
      
      int byteStart = -1;
      int byteEnd = -1;
      int charCount = 0;
      
      for (int i = 0; i < sVal.length(); i++) {
        if ((uint8_t)sVal[i] < 0x80 || (uint8_t)sVal[i] >= 0xC0) {
          if (charCount == start) {
            byteStart = i;
          }
          if (charCount == start + len) {
            byteEnd = i;
            break;
          }
          charCount++;
        }
      }
      
      if (byteStart != -1) {
        if (byteEnd == -1) byteEnd = sVal.length();
        return sVal.substring(byteStart, byteEnd);
      }
    }
    return "";
  }

  // 6. Строковая переменная
  if (upperExpr.endsWith("$")) {
    if (_stringVars.find(upperExpr) != _stringVars.end()) {
      return _stringVars[upperExpr];
    }
    return "";
  }
  
  return "";
}

void BasicEngine::runProgram() {
  if (_program.empty()) {
    _console.println("?Ошибка: Программа пуста.");
    return;
  }
  _console.clear();
  _console.println("ЗАПУСК...");
  _console.println("Остановка: Ctrl+Break");
  
  _forStack.clear(); 
  _variables.clear(); 
  _breakRequested = false;
  int pc = 0; 
  
  while (pc < _program.size()) {
    vTaskDelay(1 / portTICK_PERIOD_MS);
    
    checkBreak();
    if (_externalStopRequested) {       
      _breakRequested = true;           
      _externalStopRequested = false;   
    }                                   
    
    if (_breakRequested) {
      _console.println("");
      _console.println("?Прервано пользователем (Break).");
      break;
    }

    if (_console.isFull()) {
      _console.setSuppressScroll(true); // Запрещаем автоочистку
      _console.println("-- Экран заполнен --");
      _console.println("Нажмите Enter...");
      
      _console.waitForEnter();
      
      _console.setSuppressScroll(false); // Разрешаем автоочистку снова
      
      if (_console.checkBreak()) {
          _console.println("?Прервано.");
          break;
      }
      
      _console.clear();
      _console.println("-- Продолжение --");
    }

    String line = _program[pc].code;
    String upperLine = line;
    upperLine.toUpperCase();

    if (line.length() == 0) { pc++; continue; }

    // --- REM ---
    if (upperLine.startsWith("REM")) {
      pc++;
      continue;
    }
    
    // --- IF ---
    if (upperLine.startsWith("IF ")) {
      int thenIdx = upperLine.indexOf(" THEN ");
      if (thenIdx == -1) {
        _console.println("?Синтаксическая ошибка: пропущено THEN");
        break;
      }
      
      String conditionStr = line.substring(3, thenIdx);
      String thenPart = line.substring(thenIdx + 6);
      thenPart.trim();
      
      // Проверяем, есть ли в условии строковые элементы
      String upperCond = conditionStr;
      upperCond.toUpperCase();
      bool isStringCondition = (upperCond.indexOf("MID$") != -1 || 
                                upperCond.indexOf("LEN(") != -1 || 
                                upperCond.indexOf("ASC(") != -1 ||
                                conditionStr.indexOf('"') != -1 ||
                                upperCond.indexOf("$") != -1);
      
      bool conditionMet = false;
      int opIdx = -1;
      String op = "";
      
      // Ищем оператор сравнения
      if (conditionStr.indexOf(">=") != -1) { opIdx = conditionStr.indexOf(">="); op = ">="; }
      else if (conditionStr.indexOf("<=") != -1) { opIdx = conditionStr.indexOf("<="); op = "<="; }
      else if (conditionStr.indexOf("<>") != -1) { opIdx = conditionStr.indexOf("<>"); op = "<>"; }
      else if (conditionStr.indexOf(">") != -1) { opIdx = conditionStr.indexOf(">"); op = ">"; }
      else if (conditionStr.indexOf("<") != -1) { opIdx = conditionStr.indexOf("<"); op = "<"; }
      else if (conditionStr.indexOf("=") != -1) { opIdx = conditionStr.indexOf("="); op = "="; }
      
      if (opIdx == -1) {
        _console.println("?Синтаксическая ошибка: нет оператора сравнения");
        break;
      }
      
      String leftExpr = conditionStr.substring(0, opIdx);
      String rightExpr = conditionStr.substring(opIdx + op.length());
      
      if (isStringCondition) {
        // --- СРАВНЕНИЕ СТРОК ---
        String leftVal = evaluateStringExpr(leftExpr);
        String rightVal = evaluateStringExpr(rightExpr);
        
        if (op == "=") conditionMet = (leftVal == rightVal);
        else if (op == "<>") conditionMet = (leftVal != rightVal);
        // Для строк операции > и < в нашем упрощенном варианте не поддерживаются
        else conditionMet = false; 
      } else {
        // --- СРАВНЕНИЕ ЧИСЕЛ ---
        float leftVal = evaluateExpression(leftExpr);
        float rightVal = evaluateExpression(rightExpr);
        
        if (op == "=") conditionMet = (leftVal == rightVal);
        else if (op == "<>") conditionMet = (leftVal != rightVal);
        else if (op == ">") conditionMet = (leftVal > rightVal);
        else if (op == "<") conditionMet = (leftVal < rightVal);
        else if (op == ">=") conditionMet = (leftVal >= rightVal);
        else if (op == "<=") conditionMet = (leftVal <= rightVal);
      }
      
      if (conditionMet) {
        String thenUpper = thenPart;
        thenUpper.toUpperCase();
        
        if (thenUpper.startsWith("PRINT ") || thenUpper.startsWith("?")) {
          String arg = thenPart;
          if (thenUpper.startsWith("PRINT ")) arg = thenPart.substring(6);
          else arg = thenPart.substring(1);
          executePrint(arg);
        }
        else if (thenUpper.startsWith("GOTO ")) {
          int target = thenPart.substring(5).toInt();
          bool found = false;
          for (int i = 0; i < _program.size(); i++) {
            if (_program[i].number == target) {
              pc = i;
              found = true;
              break;
            }
          }
          if (!found) {
            _console.println("?Ошибка: Строка " + String(target) + " не найдена.");
            break;
          }
          continue; 
        }
        else if (thenUpper.startsWith("LET ") || (isAlpha(thenPart[0]) && thenPart.indexOf('=') != -1)) {
          String expr = thenPart;
          if (thenUpper.startsWith("LET ")) expr = thenPart.substring(4);
          int eqIdx = expr.indexOf('=');
          if (eqIdx != -1) {
            String leftPart = expr.substring(0, eqIdx);
            leftPart.trim();
            String valueExpr = expr.substring(eqIdx + 1);
            
            int parenIdx = leftPart.indexOf('(');
            if (parenIdx != -1) {
              // Присваивание элементу массива
              String arrName = leftPart.substring(0, parenIdx);
              arrName.trim();
              arrName.toUpperCase();
              String indicesStr = leftPart.substring(parenIdx);
              if (_arrays.find(arrName) == _arrays.end()) {
                 _console.println("?Ошибка: Массив не объявлен");
                 break;
              }
              BasicArray& arr = _arrays[arrName];
              String inner = extractParens(indicesStr);
              
              // Проверяем тип переменной (массивы у нас пока только числовые)
              float val = evaluateExpression(valueExpr);
              
              if (arr.numDims == 1) {
                int i = (int)evaluateExpression(inner);
                if (i >= 0 && i < arr.dims[0]) arr.data[i] = val;
                else _console.println("?Ошибка: Индекс вне границ");
              } else {
                int commaIdx = inner.indexOf(',');
                if (commaIdx != -1) {
                  int i = (int)evaluateExpression(inner.substring(0, commaIdx));
                  int j = (int)evaluateExpression(inner.substring(commaIdx + 1));
                  if (i >= 0 && i < arr.dims[0] && j >= 0 && j < arr.dims[1]) {
                    arr.data[i * arr.dims[1] + j] = val;
                  } else {
                    _console.println("?Ошибка: Индексы вне границ");
                  }
                }
              }
            } else {
              // Присваивание переменной
              String varName = leftPart;
              varName.trim();
              varName.toUpperCase();
              
              if (varName.endsWith("$")) {
                String strVal = evaluateStringExpr(valueExpr);
                _stringVars[varName] = strVal;
              } else {
                float val = evaluateExpression(valueExpr);
                _variables[varName] = val;
              }
            }
          }
        }
      }
      pc++;
      continue; 
    }
    // --- DIM ---
    else if (upperLine.startsWith("DIM ")) {
      String args = line.substring(4);
      args.trim();
      int parenIdx = args.indexOf('(');
      if (parenIdx == -1) {
        _console.println("?Синтаксическая ошибка: пропущены скобки в DIM");
        break;
      }
      String name = args.substring(0, parenIdx);
      name.trim();
      name.toUpperCase();
      int closeIdx = args.lastIndexOf(')');
      if (closeIdx == -1) {
        _console.println("?Синтаксическая ошибка: нет закрывающей скобки");
        break;
      }
      String dimsStr = args.substring(parenIdx + 1, closeIdx);
      dimsStr.trim();
      BasicArray arr;
      arr.numDims = 1;
      int commaIdx = dimsStr.indexOf(',');
      if (commaIdx != -1) {
        String rStr = dimsStr.substring(0, commaIdx); rStr.trim();
        String cStr = dimsStr.substring(commaIdx + 1); cStr.trim();
        int r = (int)evaluateExpression(rStr);
        int c = (int)evaluateExpression(cStr);
        arr.numDims = 2;
        arr.dims[0] = r + 1; 
        arr.dims[1] = c + 1;
        arr.data.resize(arr.dims[0] * arr.dims[1], 0.0);
        _console.println("Создан массив " + name + "(" + String(r) + "," + String(c) + ")");
      } else {
        int size = (int)evaluateExpression(dimsStr);
        arr.dims[0] = size + 1;
        arr.data.resize(arr.dims[0], 0.0);
        _console.println("Создан массив " + name + "(" + String(size) + ")");
      }
      _arrays[name] = arr;
    }
    // --- FOR ---
    else if (upperLine.startsWith("FOR ")) {
      bool isReturning = false;
      if (!_forStack.empty() && _forStack.back().startLineIndex == pc) {
        isReturning = true;
      }
      if (isReturning) {
        // Ничего не делаем
      } else {
        String args = line.substring(4);
        args.trim();
        int eqIdx = args.indexOf('=');
        int toIdx = args.indexOf(" TO ");
        if (toIdx == -1) toIdx = args.indexOf(" to ");
        if (eqIdx == -1 || toIdx == -1) {
          _console.println("?Синтаксическая ошибка: неверный формат FOR");
          break;
        }
        String varName = args.substring(0, eqIdx);
        varName.trim();
        varName.toUpperCase();
        String startExpr = args.substring(eqIdx + 1, toIdx);
        String rest = args.substring(toIdx + 4);
        rest.trim();
        float targetVal = 0;
        float stepVal = 1.0;
        int stepIdx = rest.indexOf(" STEP ");
        if (stepIdx == -1) stepIdx = rest.indexOf(" step ");
        if (stepIdx != -1) {
          targetVal = evaluateExpression(rest.substring(0, stepIdx));
          stepVal = evaluateExpression(rest.substring(stepIdx + 6));
        } else {
          targetVal = evaluateExpression(rest);
        }
        float startVal = evaluateExpression(startExpr);
        bool shouldRun = true;
        if (stepVal > 0 && startVal > targetVal) shouldRun = false;
        if (stepVal < 0 && startVal < targetVal) shouldRun = false;
        if (stepVal == 0) shouldRun = false;
        if (!shouldRun) {
          int nextIdx = findMatchingNext(pc);
          if (nextIdx != -1) {
            pc = nextIdx + 1;
            continue; 
          } else {
            _console.println("?Ошибка: не найден NEXT для FOR");
            break;
          }
        }
        ForLoopState state;
        state.varName = varName;
        state.targetValue = targetVal;
        state.stepValue = stepVal;
        state.startLineIndex = pc;
        _variables[varName] = startVal;
        _forStack.push_back(state);
      }
    }
        // --- NEXT ---
    else if (upperLine.startsWith("NEXT")) {
      if (_forStack.empty()) {
        _console.println("?Ошибка: NEXT без FOR");
        break;
      }
      
      ForLoopState& state = _forStack.back();
      _variables[state.varName] += state.stepValue;
      
      bool continueLoop = false;
      if (state.stepValue > 0) {
        if (_variables[state.varName] <= state.targetValue) continueLoop = true;
      } else {
        if (_variables[state.varName] >= state.targetValue) continueLoop = true;
      }
      
      if (continueLoop) {
        pc = state.startLineIndex;
        continue; 
      } else {
        _forStack.pop_back();
      }
    }

    // --- PAUSE ---
    else if (upperLine.startsWith("PAUSE ")) {
      String arg = line.substring(6);
      float seconds = evaluateExpression(arg);
      if (seconds > 0) {
        delay((unsigned long)(seconds * 1000));
      }
    }
    // --- INPUT ---
    else if (upperLine.startsWith("INPUT ")) {
      String rest = line.substring(6);
      rest.trim();
      
      String varName = rest;
      String prompt = "";
      
      int quoteStart = rest.indexOf('"');
      if (quoteStart != -1) {
        int quoteEnd = rest.indexOf('"', quoteStart + 1);
        if (quoteEnd != -1) {
          prompt = rest.substring(quoteStart + 1, quoteEnd);
          _console.print(prompt + " ");
          
          int commaIdx = rest.indexOf(',', quoteEnd);
          if (commaIdx != -1) {
            varName = rest.substring(commaIdx + 1);
            varName.trim();
          }
        }
      } else {
         _console.print("? ");
      }
      
      // --- ПЕРЕДАЕМ ТЕКСТ ПРИГЛАШЕНИЯ В ВЕБ-ИНТЕРФЕЙС ---
      extern String globalPrompt;
      globalPrompt = prompt.length() > 0 ? prompt : "Введите значение:";
      // ---------------------------------------------------
      
      varName.toUpperCase();
      
      String userInput = _console.getInputString();
      _console.println(""); 
      
      if (userInput == "BREAK" || _breakRequested) {
        _console.println("?Прервано (Break).");
        break;
      }
      
      if (varName.endsWith("$")) {
        _stringVars[varName] = userInput;
      } else {
        float val = userInput.toFloat();
        _variables[varName] = val;
      }
    }
    // --- PRINT ---
    else if (upperLine.startsWith("PRINT ") || upperLine.startsWith("?")) {
      String arg;
      if (upperLine.startsWith("PRINT ")) arg = line.substring(6);
      else arg = line.substring(1);
      executePrint(arg);
    } 
    // --- LET / Присваивание ---
    else if (upperLine.startsWith("LET ") || (isAlpha(line[0]) && line.indexOf('=') != -1)) {
      String expr = line;
      if (upperLine.startsWith("LET ")) expr = line.substring(4);
      
      int eqIdx = expr.indexOf('=');
      if (eqIdx != -1) {
        String leftPart = expr.substring(0, eqIdx);
        leftPart.trim();
        String valueExpr = expr.substring(eqIdx + 1);
        valueExpr.trim();
        
        int parenIdx = leftPart.indexOf('(');
        if (parenIdx != -1) {
          // --- ОБРАБОТКА МАССИВОВ ---
          String arrName = leftPart.substring(0, parenIdx);
          arrName.trim();
          arrName.toUpperCase();
          arrName.replace("\r", "");
          arrName.replace("\n", "");
          
          String indicesStr = leftPart.substring(parenIdx);
          if (_arrays.find(arrName) == _arrays.end()) {
             _console.println("?Ошибка: Массив не объявлен");
             break;
          }
          float val = evaluateExpression(valueExpr); 
          BasicArray& arr = _arrays[arrName];
          String inner = extractParens(indicesStr);
          if (arr.numDims == 1) {
            int i = (int)evaluateExpression(inner);
            if (i >= 0 && i < arr.dims[0]) arr.data[i] = val;
            else _console.println("?Ошибка: Индекс вне границ");
          } else {
            int commaIdx = inner.indexOf(',');
            if (commaIdx != -1) {
              int i = (int)evaluateExpression(inner.substring(0, commaIdx));
              int j = (int)evaluateExpression(inner.substring(commaIdx + 1));
              if (i >= 0 && i < arr.dims[0] && j >= 0 && j < arr.dims[1]) {
                arr.data[i * arr.dims[1] + j] = val;
              } else {
                _console.println("?Ошибка: Индексы вне границ");
              }
            }
          }
        } else {
          // --- ОБРАБОТКА ПЕРЕМЕННЫХ ---
          String varName = leftPart;
          varName.trim();
          varName.toUpperCase();
          varName.replace("\r", "");
          varName.replace("\n", "");
          
          if (varName.endsWith("$")) {
            String strVal = evaluateStringExpr(valueExpr);
            _stringVars[varName] = strVal;
          } else {
            float val = evaluateExpression(valueExpr);
            _variables[varName] = val;
          }
        }
      }
    }
    // --- GOTO ---
    else if (upperLine.startsWith("GOTO ")) {
      int target = line.substring(5).toInt();
      bool found = false;
      for (int i = 0; i < _program.size(); i++) {
        if (_program[i].number == target) {
          pc = i;
          found = true;
          break;
        }
      }
      if (!found) {
        _console.println("?Ошибка: Строка " + String(target) + " не найдена.");
        break;
      }
      continue; 
    }
    // --- END ---
    else if (upperLine == "END") {
      break;
    }
    // --- CLS ---
    else if (upperLine == "CLS") {
      _console.clear();
    }
    // --- ГРАФИКА ---
    else if (upperLine.startsWith("COLOR ")) {
      int c = (int)evaluateExpression(line.substring(6));
      _currentColor = getColor(c);
    }
    else if (upperLine.startsWith("PLOT ")) {
      String args = line.substring(5);
      int c1 = args.indexOf(',');
      if (c1 != -1) {
        float x = evaluateExpression(args.substring(0, c1));
        float y = evaluateExpression(args.substring(c1 + 1));
        _tft.drawPixel((int)x, (int)y, _currentColor);
      }
    }
    else if (upperLine.startsWith("LINE ")) {
      String args = line.substring(5);
      int c1 = args.indexOf(',');
      int c2 = args.indexOf(',', c1 + 1);
      int c3 = args.indexOf(',', c2 + 1);
      if (c1 != -1 && c2 != -1 && c3 != -1) {
        float x1 = evaluateExpression(args.substring(0, c1));
        float y1 = evaluateExpression(args.substring(c1 + 1, c2));
        float x2 = evaluateExpression(args.substring(c2 + 1, c3));
        float y2 = evaluateExpression(args.substring(c3 + 1));
        _tft.drawLine((int)x1, (int)y1, (int)x2, (int)y2, _currentColor);
      }
    }
    else if (upperLine.startsWith("RECT ")) {
      String args = line.substring(5);
      int c1 = args.indexOf(',');
      int c2 = args.indexOf(',', c1 + 1);
      int c3 = args.indexOf(',', c2 + 1);
      if (c1 != -1 && c2 != -1 && c3 != -1) {
        float x1 = evaluateExpression(args.substring(0, c1));
        float y1 = evaluateExpression(args.substring(c1 + 1, c2));
        float x2 = evaluateExpression(args.substring(c2 + 1, c3));
        float y2 = evaluateExpression(args.substring(c3 + 1));
        _tft.drawRect((int)x1, (int)y1, (int)(x2 - x1), (int)(y2 - y1), _currentColor);
      }
    }
        else if (upperLine.startsWith("CIRCLE ")) {
      String args = line.substring(7);
      
      int c1 = args.indexOf(',');
      int c2 = args.indexOf(',', c1 + 1);
      int c3 = args.indexOf(',', c2 + 1);
      int c4 = args.indexOf(',', c3 + 1);
      int c5 = args.indexOf(',', c4 + 1);
      int c6 = args.indexOf(',', c5 + 1);
      
      float x = 0, y = 0, r = 0;
      float startAng = 0, endAng = 6.283185; 
      float aspect = 1.0;
      int axis = 1; // 1 = сжатие по Y, 2 = сжатие по X
      
      if (c2 != -1) {
        x = evaluateExpression(args.substring(0, c1));
        y = evaluateExpression(args.substring(c1 + 1, c2));
        r = evaluateExpression(args.substring(c2 + 1, c3 != -1 ? c3 : args.length()));
        
        if (c3 != -1) { 
          startAng = evaluateExpression(args.substring(c3 + 1, c4 != -1 ? c4 : args.length()));
          endAng = evaluateExpression(args.substring(c4 + 1, c5 != -1 ? c5 : args.length()));
          
          if (c5 != -1) { 
            aspect = evaluateExpression(args.substring(c5 + 1, c6 != -1 ? c6 : args.length()));
            
            if (c6 != -1) { // Новый параметр: ось сжатия
              axis = (int)evaluateExpression(args.substring(c6 + 1));
            }
          }
        }
        
        // Рисуем дугу или эллипс (если aspect не 1.0)
        if (startAng != 0 || endAng != 6.283185 || aspect != 1.0) {
          int segments = 100; 
          float angleStep = (endAng - startAng) / segments;
          
          float prevX, prevY;
          // Вычисляем начальную точку с учетом оси сжатия
          if (axis == 2) { // Сжатие по X
            prevX = x + r * aspect * cos(startAng);
            prevY = y - r * sin(startAng);
          } else { // Сжатие по Y (по умолчанию)
            prevX = x + r * cos(startAng);
            prevY = y - r * aspect * sin(startAng);
          }
          
          for (int i = 1; i <= segments; i++) {
            float angle = startAng + i * angleStep;
            float currentX, currentY;
            
            if (axis == 2) { // Сжатие по X
              currentX = x + r * aspect * cos(angle);
              currentY = y - r * sin(angle);
            } else { // Сжатие по Y
              currentX = x + r * cos(angle);
              currentY = y - r * aspect * sin(angle);
            }
            
            _tft.drawLine((int)prevX, (int)prevY, (int)currentX, (int)currentY, _currentColor);
            
            prevX = currentX;
            prevY = currentY;
          }
        } else {
          _tft.drawCircle((int)x, (int)y, (int)r, _currentColor);
        }
      }
    }
    else if (upperLine.startsWith("POINT ")) {
      String args = line.substring(6);
      int c1 = args.indexOf(',');
      if (c1 != -1) {
        float x = evaluateExpression(args.substring(0, c1));
        float y = evaluateExpression(args.substring(c1 + 1));
        uint16_t color = _tft.readPixel((int)x, (int)y);
        _variables["POINT_COLOR"] = (float)color; 
      }
    }
    else if (upperLine.startsWith("PAINT ")) {
      String args = line.substring(6);
      int c1 = args.indexOf(',');
      if (c1 != -1) {
        float x = evaluateExpression(args.substring(0, c1));
        float y = evaluateExpression(args.substring(c1 + 1));
        floodFill((int)x, (int)y, _currentColor); 
      }
    }
    else if (upperLine.startsWith("TEXT ")) {
      String args = line.substring(5);
      int c1 = args.indexOf(',');
      int c2 = args.indexOf(',', c1 + 1);
      if (c1 != -1 && c2 != -1) {
        float x = evaluateExpression(args.substring(0, c1));
        float y = evaluateExpression(args.substring(c1 + 1, c2));
        String strVal = evaluateStringExpr(args.substring(c2 + 1));
        _tft.setTextColor(_currentColor, TFT_BLACK);
        _tft.drawString(strVal, (int)x, (int)y);
      }
    }
    // --- Неизвестная команда ---
    else {
      _console.println("?Синтаксическая ошибка: " + line);
      break;
    }

    pc++;
    delay(10); 
  }

  _console.println("Готово.");
}

void BasicEngine::printHelp() {
  _console.println("Команды:");
  _console.println("  <номер> <код> - ввод строки программы");
  _console.println("  LIST          - вывести программу");
  _console.println("  RUN           - запустить программу");
  _console.println("  DEL <номер>   - удалить строку");
  _console.println("  NEW           - очистить программу");
  _console.println("  CLS           - очистить экран");
  _console.println("  OPS           - список операторов");
  _console.println("  HELP          - эта справка");
}

void BasicEngine::printHelpPage(int pageIndex) {
  _console.clear();
  String page = String(_helpPages[pageIndex]);
  
  int start = 0;
  while (start < page.length()) {
    int end = page.indexOf('\n', start);
    if (end == -1) end = page.length();
    _console.println(page.substring(start, end));
    start = end + 1;
  }
}

void BasicEngine::printOps() {
  _console.setSuppressScroll(true); // Отключаем автоочистку
  _isHelpListing = true;
  _helpPage = 0;
  _console.clear();
  printHelpPage(_helpPage);
}