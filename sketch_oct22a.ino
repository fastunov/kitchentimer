#include <Wire.h>
#include <TM1637.h>         // http://www.seeedstudio.com/wiki/File:DigitalTube.zip
#include <Bounce2.h>        // https://github.com/thomasfredericks/Bounce2
#include <TimerOne.h>       // https://github.com/PaulStoffregen/TimerOne

// Индикатор
#define CLK 6               // Пин для шины индикатора
#define DIO 7               // Пин для шины индикатора
#define BRIGHTNESS 4        // яркость, от 0 до 7
TM1637 tm1637(CLK,DIO);   

//Пищалка
#define BUZZER_PIN 8        // Пин пищалки
#define BUZZER_FREQ 800     // Частота звука пищалки

// Кнопки
#define KEY_MODE 5           // Кнопка переключения режимов
#define KEY_LEFT 4           // Кнопка левая 
#define KEY_RIGHT 3          // Кнопка правая

Bounce  bounceKEY_MODE = Bounce(); // Объекты для подавления дребезга контактов
Bounce  bounceKEY_LEFT = Bounce();
Bounce  bounceKEY_RIGHT = Bounce();

volatile byte KeyModeState;   // Переменные сосотояния клавиш
volatile byte KeyLeftState;   // 0 - клавиша отпушена
volatile byte KeyRightState;  // 1 и более - количество тиков таймера (0.1 сек), когда удерживается клавиша. 
                              // 1 присваиваем при нажатии (погрешность в 0.1 сек укладывается в ТЗ на таймер)

volatile byte KeyModeShort;   // Флаг короткого нажатия на кнопки. 
volatile byte KeyLeftShort;   // 1 - было короткое нажатие на кнопку
volatile byte KeyRightShort;  // 
                              
#define BOUNCE_INTERVAL 10      // Интервал в миллисекундах для подавления дребезга контактов
#define LONG_PRESS  20        // Определяем длительное нажатие (десятые секунды)

// Индикаторы
#define IND_CLOCK 9              // Индикатор режима часов
#define IND_ALARM 10             // Индикатор режима будильника
#define IND_TIMER 11             // Индикатор режима таймера
#define IND_TEMPRATURE 12        // Индикатор режима градусника

// Часы
#define DS3231_I2C_ADDRESS 0x68   // Адрес микросхемы (по даташиту)

// Временняе интервалы для разных режимов
#define TEMPDISPLAYPERIOD 5       // Период показа температуры в секундах для режима ЧАСЫ
#define TIMEDISPLAYPERIOD 10      // Период показа времени в секундах для режима часы
#define SET_BLINK_INTERVAL 5      // Период моргания светодиода для режимов установки  ( раз в секунду 0-SET_BLINK_INTERVAL) десятые секнды

// Системные переменные
volatile boolean flag;          // Флаг для минания секундами на индикаторе
volatile byte SecCounter;       // Счетчик секунд
volatile byte TicksCount;       // Счетчик тиков таймера
volatile unsigned int SystemState;       // Индикатор состояния системы
volatile unsigned int PrevSystemState;   // Предыдущее сосотояние системы 
int8_t ShowMatrix[4];           // Масив для вывода на экран
volatile byte SetHour, SetMinute;        // Переменные для установки часов и минут
volatile byte AlarmHour, AlarmMinute;    // Переменные для хранения времени срабатывания будильника
volatile byte TimerHour, TimerMinute, TimerSecond;    // Переменные для хранения времени срабатывания таймера
#define NULL_TIME 201                    // Значение для перменных срабавтывания таймера и будуильника, заведомо невозможное к установке системой 
volatile byte  IdleTimer;                // Счетчик для переключения в режим ЧАСЫ 
#define IDLE_TIME 120                    // Время переключения в режим ЧАСЫ, если система находится в сосотоянии покоя. Секунды 
     
// Маски для состояний системы 
//      МОДУЛЬ_РЕЖИМ                                
#define CLOCK_DISPLAY    0x800 // B0000100000000000  Часы, включаем вывод на экран
#define CLOCK_TIME       0x400 // B0000010000000000  Часы, вывод времени/температуры
#define CLOCK_SET        0x200 // B0000001000000000  Часы, режим установки
#define ALARM_DISPLAY    0x100 // B0000000100000000  Будильник, включаем вывод на экран
#define ALARM_ON         0x80  // B0000000010000000  Будильник, включен
#define ALARM_READY      0x40  // B0000000001000000  Будильник, сработал
#define ALARM_SET        0x20  // B0000000000100000  Будильник, режим установки
#define TIMER_DISPLAY    0x10  // B0000000000010000  Таймер, включаем вывод на экран
#define TIMER_ON         0x8   // B0000000000001000  Таймер, включен
#define TIMER_READY      0x4   // B0000000000000100  Таймер, сработал
#define TIMER_SET        0x2   // B0000000000000010  Таймер, режим установки
#define TEMP_DISPLAY     0x1   // B0000000000000001  Термометр, включаем вывод на экран                                   
                               
// Ниже идет реализация

byte decToBcd(byte val)        // Конвертация формата данных десятичный в двоичнодесятичный код
{
  return ( (val/10*16) + (val%10) );
}

byte bcdToDec(byte val)       // Конвертация данных из двичнодесятичного в десятичный код
{
  return ( (val/16*10) + (val%16) );
}

void setDateDs3231(            // Установка времени даты в часах
  byte second, // 0-59
  byte minute, // 0-59
  byte hour, // 1-23
  byte dayOfWeek, // 1-7
  byte dayOfMonth, // 1-28/29/30/31
  byte month, // 1-12
  byte year) // 0-99
{
  Wire.beginTransmission(DS3231_I2C_ADDRESS);
  Wire.write(0);
  Wire.write(decToBcd(second));
  Wire.write(decToBcd(minute));
  Wire.write(decToBcd(hour));
  Wire.write(decToBcd(dayOfWeek));
  Wire.write(decToBcd(dayOfMonth));
  Wire.write(decToBcd(month));
  Wire.write(decToBcd(year));
  Wire.endTransmission();
}

void getDateDs3231(     // Чтение даты и времени в часах    
  byte *second,
  byte *minute,
  byte *hour,
  byte *dayOfWeek,
  byte *dayOfMonth,
  byte *month,
  byte *year)
{
  Wire.beginTransmission(DS3231_I2C_ADDRESS);
  Wire.write(0);
  Wire.endTransmission();
  Wire.requestFrom(DS3231_I2C_ADDRESS, 7);
  *second = bcdToDec(Wire.read() & 0x7f);
  *minute = bcdToDec(Wire.read());
  *hour = bcdToDec(Wire.read() & 0x3f);
  *dayOfWeek = bcdToDec(Wire.read());
  *dayOfMonth = bcdToDec(Wire.read());
  *month = bcdToDec(Wire.read());
  *year = bcdToDec(Wire.read());
}

void getTempDS3231 (  // Читаем температуру из регистра часов
  byte *Temp
  )
{
  Wire.beginTransmission(DS3231_I2C_ADDRESS);
  Wire.write(0x11);
  Wire.endTransmission();
  Wire.requestFrom(DS3231_I2C_ADDRESS, 1);  // Читаем только целую часть значения. Дробная часть не интересна
  *Temp = Wire.read() & B01111111;
}

void initDS3231()              //включает выход SQW, который вроде выключен по умолчанию
{
  Wire.beginTransmission(DS3231_I2C_ADDRESS);
  Wire.write(0x0E);
  Wire.write(0x40);
  Wire.endTransmission();
}

void ShowCurrentTime(TM1637 *device)     // Выводим текущее время на экран
{
  byte second, minute, hour, dayOfWeek, dayOfMonth, month, year;
  int8_t ShowDisp[4]; //Масив для хранения текущего значения времени*

  // Читаем время из модуля
  getDateDs3231(&second, &minute, &hour, &dayOfWeek, &dayOfMonth, &month, &year);
  
  // Заполняем массив значениями для передачи на экран
  ShowDisp[0] = hour / 10;
  ShowDisp[1] = hour % 10;
  ShowDisp[2] = minute / 10;
  ShowDisp[3] = minute % 10;

  // отправляем массив на экран
  device->display(ShowDisp);
}

void ShowCurrentTemperature (TM1637 *device) // Выводим текущую темпаратуру на экран
{
  byte temperature;
  int8_t ShowDisp[4]; //Масив для хранения текущего значения времени*
        // Читаем значение температуры  
      getTempDS3231 (&temperature);
    
      // Заполняем массив значениями для передачи на экран
      ShowDisp[0] = temperature / 10;
      ShowDisp[1] = temperature % 10;
      ShowDisp[2] = 0x7f;             // Очищаем позицию на индикаторе
      ShowDisp[3] = 0xC;              // Символ С
    
      // отправляем массив на экран
      device->display(ShowDisp);

}

void GetCurrentTime(int8_t *Matrix)   // Получаем текущее значение времени 
{
  byte second, minute, hour, dayOfWeek, dayOfMonth, month, year;

  // Читаем время из модуля
  getDateDs3231(&second, &minute, &hour, &dayOfWeek, &dayOfMonth, &month, &year);
  
  // Заполняем массив значениями для передачи на экран
  Matrix[0] = hour / 10;
  Matrix[1] = hour % 10;
  Matrix[2] = minute / 10;
  Matrix[3] = minute % 10; 
}

void blink()             // Выполняется  каждую секунду
{
 byte second, minute, hour, dayOfWeek, dayOfMonth, month, year, temperature;
 int8_t ShowDisp[4]; //Масив для хранения текущего значения времени*

 digitalWrite(13, !digitalRead(13)); // Моргаем светодиодом на контроллере (Для контроля работы контроллера)
 interrupts();                       // Разрешаем прерывания в обработчике прерываний для работы Wire

 SecCounter++;    // Считаем секунды

 if ((SystemState & ALARM_ON) > 0) // Включен будильник
 {
  // Читаем время из модуля
  getDateDs3231(&second, &minute, &hour, &dayOfWeek, &dayOfMonth, &month, &year);

  if ((AlarmHour == hour) && (AlarmMinute == minute)) // Время совпало
  {
      PrevSystemState = SystemState;
      SystemState = SystemState & ~ (CLOCK_DISPLAY | TIMER_DISPLAY | TEMP_DISPLAY | CLOCK_TIME | CLOCK_SET | ALARM_ON | ALARM_SET | TIMER_SET);
      SystemState = SystemState | ALARM_DISPLAY | ALARM_READY;                  // Переводим в режим индикации будильника
  }
 }

 if ((SystemState & TIMER_ON) > 0)  // Включен таймер. Уменьшаем счетчик таймера
 { 
  if (TimerSecond == 0)
  {
   if (TimerMinute == 0)
   {
    if (TimerHour == 0)
    {
      // Отсчет времени закончен 
      SystemState = SystemState & ~ TIMER_ON;
      SystemState = SystemState | TIMER_READY;
      
      PrevSystemState = SystemState;
      SystemState = SystemState & ~(CLOCK_DISPLAY | ALARM_DISPLAY | TEMP_DISPLAY | CLOCK_TIME | CLOCK_SET | ALARM_SET | TIMER_ON | TIMER_SET);                                            
      SystemState = SystemState | TIMER_DISPLAY | TIMER_READY;                  // Переводим в режим индикации таймера   
    }
    else
    {
      TimerHour--;
      TimerMinute = 59; 
    }   
   }
   else
   {
    TimerMinute--;
    TimerSecond = 59;
   }
  }
  else
    TimerSecond--;   
 }

 if ((SystemState & CLOCK_DISPLAY) > 0)   // Режим ЧАСЫ 
 {
  IdleTimer = 0;
  if ((SystemState & CLOCK_TIME) > 0)     // Выводим время
  {
    flag = !flag;
    tm1637.point(flag);                   // Моргаем разделителем на экране
    if (SecCounter > TIMEDISPLAYPERIOD)   // Пора вывести температуру
    {
      SystemState = SystemState & ~ CLOCK_TIME; // Меняем бит индикации
      SecCounter = 0; 
      tm1637.point(0);                    // Выключаем разделитель
    } 
    else
    { 
      ShowCurrentTime(&tm1637);           // Выводим текущее время
    }  
  }
  else 
  {
    if (SecCounter > TEMPDISPLAYPERIOD)
    {
      SystemState = SystemState ^ CLOCK_TIME; // Меняем бит индикации
      SecCounter = 0;
    }
    else
    {
      ShowCurrentTemperature (&tm1637);   // Выводим температуру        
    }
  }
 } 
 else
 if ((SystemState & TEMP_DISPLAY) > 0)    // Режим ТЕРМОМЕТР
 {
    IdleTimer = 0;
    flag = !flag;
    tm1637.point(flag);                   // Моргаем разделителем на экране 
    ShowCurrentTemperature (&tm1637);     // Выводим температуру                               
 }
 else
 if ((SystemState & ALARM_DISPLAY) > 0)   // Режим БУДИЛЬНИК 
 {
    if ((SystemState & ALARM_READY)>0) IdleTimer = 0; // Система будет находиться в состоянии БУДИЛЬНИК СРАБОТАЛ до нажатия клавиши
      
    tm1637.point(1);                      // Включаем разделитель
    if (AlarmHour >= NULL_TIME || AlarmMinute >= NULL_TIME)
    {                                     // Если будильник еще не установлен
      GetCurrentTime (ShowDisp);          // Заполняем матрицу времени для вывода
    }
    else
    {                                     // Будильник установлен
      ShowDisp[0] = AlarmHour / 10;
      ShowDisp[1] = AlarmHour % 10;
      ShowDisp[2] = AlarmMinute / 10;
      ShowDisp[3] = AlarmMinute % 10;              
    }
    tm1637.display (ShowDisp);   
 }
 else
 if ((SystemState & TIMER_DISPLAY) > 0)   // Режим ТАЙМЕР 
 {
    if ((SystemState & TIMER_READY) > 0  || (SystemState & TIMER_ON) > 0) IdleTimer = 0; // Система будет находиться в состоянии ТАЙМЕР СРАБОТАЛ или ТАЙМЕР СЧИТАЕТ до нажатия клавиши
    
    if (TimerHour >= NULL_TIME || TimerMinute >= NULL_TIME)
    {                                         // Если таймер еще не установлен
      ShowDisp[0] = 0;
      ShowDisp[1] = 0;
      ShowDisp[2] = 0;
      ShowDisp[3] = 0;
    }
    else
    {                                          // Для ииндикации будем использовать установленное время срабатывания таймера
      ShowDisp[0] = TimerHour / 10;
      ShowDisp[1] = TimerHour % 10;
      ShowDisp[2] = TimerMinute / 10;
      ShowDisp[3] = TimerMinute % 10;        
    }    
    tm1637.display (ShowDisp); 
      
    if ((SystemState & TIMER_ON) > 0)       // Если таймер включен
    {
      tm1637.point(SecCounter%2);           // Моргаем разделителем
    }
    else
      tm1637.point(1);                      // Включаем разделитель
 }

 IdleTimer++;
 if (IdleTimer > IDLE_TIME)                 // Систему никто не трогал долго
 {
   PrevSystemState = SystemState;
   SystemState = SystemState & ~ ( ALARM_DISPLAY | TIMER_DISPLAY | TEMP_DISPLAY | CLOCK_TIME | CLOCK_SET | ALARM_SET | TIMER_SET);
   SystemState = SystemState | CLOCK_DISPLAY;                  // Переводим в режим ЧАСЫ 
   IdleTimer = 0;
 }
}

void TimerTick(void)        // Функция выполняется каждые 0.1 сек
{
  unsigned int LED_Status;  // Вспомогательная переменная для определения статуса светодиодов
  byte second, minute, hour, dayOfWeek, dayOfMonth, month, year; // Переменные для получения данных из DS3231 

  interrupts();             // Разрешаем прерывания для работы Wire
    
  if (TicksCount>8) // Обрабатываем счетчик тиков
    TicksCount = 0;
  else 
    TicksCount++;   // Увеличиваем счетчик тиков  
    
  if (KeyModeState > 0) KeyModeState++; // Увеличиваем счетчики удержания клавиш
  if (KeyLeftState > 0) KeyLeftState++; 
  if (KeyRightState > 0) KeyRightState++;

  // Переключаем светодиоды до изменения состояния системы,
  // чтобы отразить измеенения состояния светодиодов в соответствии с предыдущим тиком
  if (PrevSystemState != SystemState)
  {
    // Отключаем в статусе не нужные биты. И включаем идикацию режима
    LED_Status = SystemState & ~(CLOCK_TIME | CLOCK_SET | ALARM_ON | ALARM_READY | ALARM_SET | TIMER_ON | TIMER_READY | TIMER_SET);  
    if (LED_Status > 0)
      switch (LED_Status)
      {
        case CLOCK_DISPLAY:
          digitalWrite(IND_CLOCK, 1);
          digitalWrite(IND_ALARM, 0);
          digitalWrite(IND_TIMER, 0);
          digitalWrite(IND_TEMPRATURE, 0);
          break;
        case ALARM_DISPLAY:
          digitalWrite(IND_CLOCK, 0);
          digitalWrite(IND_ALARM, 1);
          digitalWrite(IND_TIMER, 0);
          digitalWrite(IND_TEMPRATURE, 0);
          break;
        case TIMER_DISPLAY:
          digitalWrite(IND_CLOCK, 0);
          digitalWrite(IND_ALARM, 0);
          digitalWrite(IND_TIMER, 1);
          digitalWrite(IND_TEMPRATURE, 0);
          break;        
        case TEMP_DISPLAY:
          digitalWrite(IND_CLOCK, 0);
          digitalWrite(IND_ALARM, 0);
          digitalWrite(IND_TIMER, 0);
          digitalWrite(IND_TEMPRATURE, 1);
          break;        
      }
  }

  if ((SystemState & TIMER_READY) > 0)          // Сработал таймер
  {
    switch (TicksCount) 
    {  
    case 0:
    case 2:
    case 4:
    case 6:
      digitalWrite(IND_TIMER, 1);
      tone(BUZZER_PIN, BUZZER_FREQ);
      break;
    case 1:
    case 3:
    case 5:
    case 7:
      digitalWrite(IND_TIMER, 0);
      noTone(BUZZER_PIN);
    }
      
    if (KeyModeState > 0 || KeyLeftState > 0 || KeyRightState > 0) // Будильник выключаем любой клавишей
    {
      noTone(BUZZER_PIN);

      KeyModeState = 0;   
      KeyLeftState = 0;   
      KeyRightState = 0;  
      KeyModeShort = 0;   
      KeyLeftShort = 0;   
      KeyRightShort = 0;

      PrevSystemState = SystemState;
      SystemState = SystemState & ~(CLOCK_DISPLAY | ALARM_DISPLAY | TEMP_DISPLAY | CLOCK_TIME | CLOCK_SET | ALARM_SET | TIMER_ON | TIMER_READY | TIMER_SET);                                            
      SystemState = SystemState | TIMER_DISPLAY;                  // Переводим в режим индикации таймера
    }   
  }
  else
  if ((SystemState & ALARM_READY) > 0)          // Сработал будильник
  {
    switch (TicksCount) 
    {  
    case 0:
      digitalWrite(IND_ALARM, 1);
      tone(BUZZER_PIN, BUZZER_FREQ);
      break;
    case 1:
    case 3:
    case 7:
      digitalWrite(IND_ALARM, 0);
      break;
    case 2:
    case 4:
    case 6:
      digitalWrite(IND_ALARM, 1);
      break;  
    case 5:
      digitalWrite(IND_ALARM, 0);
      noTone(BUZZER_PIN);
    }  
    if (KeyModeState > 0 || KeyLeftState > 0 || KeyRightState > 0) // Будильник выключаем любой клавишей
    {
      noTone(BUZZER_PIN);

      KeyModeState = 0;   
      KeyLeftState = 0;   
      KeyRightState = 0;  
      KeyModeShort = 0;   
      KeyLeftShort = 0;   
      KeyRightShort = 0;
      
      PrevSystemState = SystemState;
      SystemState = SystemState & ~ (CLOCK_DISPLAY | TIMER_DISPLAY | TEMP_DISPLAY | CLOCK_TIME | CLOCK_SET | ALARM_ON | ALARM_READY | ALARM_SET | TIMER_SET);
      SystemState = SystemState | ALARM_DISPLAY;                  // Переводим в режим индикации будильника
    }   
  }
  else
  {
  if (!((SystemState & ALARM_SET) > 0) && ((SystemState & ALARM_ON) > 0)) // Включен будильник. Моргаем индикатором
  {
    switch (TicksCount)
    {
      case 0:
      case 4:
        digitalWrite(IND_ALARM, 1);
        break;
      case 2:
      case 6:
        digitalWrite(IND_ALARM, 0);
    }
  }

  if (!((SystemState & TIMER_SET) > 0) && ((SystemState & TIMER_ON) > 0)) // Включен таймер. Моргаем индикатором
  {
    switch (TicksCount)
    {
      case 0:
      case 4:
        digitalWrite(IND_TIMER, 1);
        break;
      case 2:
      case 6:
        digitalWrite(IND_TIMER, 0);
    }
  }

  if ((SystemState & CLOCK_SET) > 0)          // Режим установки часов
  {
    if (PrevSystemState != SystemState)       // Момент перехода из друго режима в режим установки часов  
    {
      digitalWrite(IND_ALARM, 0);             // Отключаем светодиоды при переходе в режим установки 
      digitalWrite(IND_TIMER, 0);
      digitalWrite(IND_TEMPRATURE, 0);
      
      GetCurrentTime (ShowMatrix);            // Заполняем матрицу времени для вывода
      SetHour = ShowMatrix[0]*10 + ShowMatrix[1]; // Заполняем переменные для часов и минут
      SetMinute = ShowMatrix[2]*10 + ShowMatrix[3];
    }

    PrevSystemState = SystemState; 
    
    switch (TicksCount)                       // Моргаем светодиодом режима ЧАСЫ и индикатором
    {
      case 0:                                 // На тике 0 включаем все
        tm1637.point(1);
        ShowMatrix[0] = SetHour / 10;
        ShowMatrix[1] = SetHour % 10;
        ShowMatrix[2] = SetMinute / 10;
        ShowMatrix[3] = SetMinute % 10;      
        tm1637.display (ShowMatrix);                    
        digitalWrite(IND_CLOCK, 1);           
        break;
      case SET_BLINK_INTERVAL:                 // На данном тике всё выключаем
        tm1637.point(0);
        ShowMatrix[0] = 0x7f;
        ShowMatrix[1] = 0x7f;
        ShowMatrix[2] = 0x7f;
        ShowMatrix[3] = 0x7f;
        tm1637.display (ShowMatrix);
        digitalWrite(IND_CLOCK, 0);
    }
    
    // Обрабатываем нажатия клавиш 
    if (KeyModeShort > 0)                       // Краткое нажатие на клавишу MODE
    {
      setDateDs3231( 0, SetMinute, SetHour, 1, 1, 1, 0);    // Записываем значение времени в часы (важны только часы и минуты)   
      SystemState = SystemState & ~ CLOCK_SET;    // Отключаем режим установки времени
      SystemState = SystemState | (CLOCK_DISPLAY | CLOCK_TIME);  // Переводим в режим ЧАСЫ вывод времени
      KeyModeShort = 0;
    }
    else     
    if (KeyLeftShort > 0)                      // Кратко нажали левую клавишу
    {     
      SetHour++;                                // Увеличиваем значение часов
      if (SetHour>23) SetHour = 1;
      ShowMatrix[0] = SetHour / 10;             // Правим значения в матрице вывода
      ShowMatrix[1] = SetHour % 10;
      KeyLeftShort = 0;
    }else     
    if (KeyRightShort > 0)                      // Кратко нажали правую клавишу
    {
      SetMinute++;                              // Увtkичиваем значение  минут
      if (SetMinute>59) SetMinute = 0; 
      ShowMatrix[2] = SetMinute / 10;           // Правим значения в матрице вывода
      ShowMatrix[3] = SetMinute % 10; 
      KeyRightShort = 0;
    }        
  }
  else
  if ((SystemState & ALARM_SET) > 0)            // Режим установки будильника
  {   
    if (PrevSystemState != SystemState)         // Отключаем светодиоды при переходе в режим установки  
    {
      digitalWrite(IND_CLOCK, 0);
      digitalWrite(IND_TIMER, 0);
      digitalWrite(IND_TEMPRATURE, 0);

      if (AlarmHour >= NULL_TIME || AlarmMinute >= NULL_TIME)
      {                                         // Если будильник еще не установлен
        GetCurrentTime (ShowMatrix);            // Заполняем матрицу времени для вывода
        SetHour = ShowMatrix[0]*10 + ShowMatrix[1]; // Заполняем переменные для часов и минут
        SetMinute = ShowMatrix[2]*10 + ShowMatrix[3];
      }
      else
      {                                         // Для ииндикации будем использовать установленное время срабатывания будульника
        SetHour = AlarmHour;
        SetMinute = AlarmMinute;        
      }
    }

    PrevSystemState = SystemState; 
    
    switch (TicksCount)                       // Моргаем светодиодом режима и инжикатором
    {
      case 0:                                 // На 0 тике включаем   
        tm1637.point(1);
        ShowMatrix[0] = SetHour / 10;
        ShowMatrix[1] = SetHour % 10;
        ShowMatrix[2] = SetMinute / 10;
        ShowMatrix[3] = SetMinute % 10;      
        tm1637.display (ShowMatrix);      
        digitalWrite(IND_ALARM, 1);
        break;
      case SET_BLINK_INTERVAL:                 // На данном выключаем
        tm1637.point(0);
        ShowMatrix[0] = 0x7f;
        ShowMatrix[1] = 0x7f;
        ShowMatrix[2] = 0x7f;
        ShowMatrix[3] = 0x7f;
        tm1637.display (ShowMatrix);      
        digitalWrite(IND_ALARM, 0);
    }
   // Обрабатываем нажатия клавиш 
    if (KeyModeShort > 0)                        // Краткое нажатие на клавишу MODE
    {       
      AlarmHour = SetHour;                        // Включаем будильник
      AlarmMinute = SetMinute;    
      SystemState = SystemState & ~( ALARM_SET | ALARM_READY);  // Отключаем режим установки времени и бит срабатывания будильника      
      SystemState = SystemState | (ALARM_DISPLAY | ALARM_ON);  // Переводим в режим БУДИЛЬНИК и включаем его
      KeyModeShort = 0; 
      KeyLeftShort = 0;
      KeyRightShort = 0;    
    }
    else     
    if (KeyLeftShort > 0){   // Кратко нажали левую клавишу
      SetHour++;             // Увеличиваем значение часов
      if (SetHour>23) SetHour = 1;
      ShowMatrix[0] = SetHour / 10; // Правим значения в матрице вывода
      ShowMatrix[1] = SetHour % 10;
      KeyLeftShort = 0;
    }
    else     
    if (KeyRightShort > 0){   // Кратко нажали правую клавишу
      SetMinute++;            // Увtkичиваем значение  минут
      if (SetMinute>59) SetMinute = 0; 
      ShowMatrix[2] = SetMinute / 10; // Правим значения в матрице вывода
      ShowMatrix[3] = SetMinute % 10; 
      KeyRightShort = 0;
    }
    else
    if (KeyModeState>LONG_PRESS) // Длительное нажатие клавиши MODE
    {
      AlarmHour = NULL_TIME;                                     // Выключаем будильник
      AlarmMinute = NULL_TIME;  
      SystemState = SystemState & ~ ( ALARM_SET | ALARM_READY | ALARM_ON);  // Отключаем режим установки времени, бит срабатывания будульника и бит включения будульника
      SystemState = SystemState | ALARM_DISPLAY;  // Переводим в режим БУДИЛЬНИК и включаем его
      KeyModeShort = 0;
      KeyModeState = 0;
      KeyLeftShort = 0;
      KeyRightShort = 0;       
    }
  } 
  else
  if ((SystemState & TIMER_SET) > 0)           // Режим установки таймера  
  {
    if (PrevSystemState != SystemState)        // Отключаем светодиоды при переходе в режим установки  
    {
      digitalWrite(IND_CLOCK, 0);
      digitalWrite(IND_ALARM, 0);
      digitalWrite(IND_TEMPRATURE, 0);
      
      if (TimerHour >= NULL_TIME || TimerMinute >= NULL_TIME)
      {                                         // Если таймер еще не установлен
        SetHour = 0;
        SetMinute = 0;
      }
      else
      {                                          // Для ииндикации будем использовать установленное время срабатывания таймера
        SetHour = TimerHour;
        SetMinute = TimerMinute;        
      }
      
    }

    PrevSystemState = SystemState; 
    
    switch (TicksCount)                        // Моргаем светодиодом режима и индикатором
    {
      case 0:                                  // На 0 тике включаем   
        tm1637.point(1);
        ShowMatrix[0] = SetHour / 10;
        ShowMatrix[1] = SetHour % 10;
        ShowMatrix[2] = SetMinute / 10;
        ShowMatrix[3] = SetMinute % 10;      
        tm1637.display (ShowMatrix);      
        digitalWrite(IND_TIMER, 1);
        break;
      case SET_BLINK_INTERVAL:                  // На данном выключаем
        tm1637.point(0);
        ShowMatrix[0] = 0x7f;
        ShowMatrix[1] = 0x7f;
        ShowMatrix[2] = 0x7f;
        ShowMatrix[3] = 0x7f;
        tm1637.display (ShowMatrix);      
        digitalWrite(IND_TIMER, 0);      
    }
   // Обрабатываем нажатия клавиш 
    if ((KeyModeShort > 0) && ~(SetHour==0 && SetMinute==0))   // Краткое нажатие на клавишу MODE
    { 
      TimerHour = SetHour;                                     // Включаем таймер
      TimerMinute = SetMinute;
      TimerSecond = 0;     
      SystemState = SystemState & ~ ( TIMER_SET | TIMER_READY);  // Отключаем режим установки времени и бит срабатывания таймера
      SystemState = SystemState | (TIMER_DISPLAY | TIMER_ON ); // Переводим в режим ТАЙМЕР и включаем его
      KeyModeShort = 0;
      KeyLeftShort = 0;
      KeyRightShort = 0; 
    }
    else     
    if (KeyLeftShort > 0){   // Кратко нажали левую клавишу
      SetHour++;             // Увеличиваем значение часов
      if (SetHour>99) SetHour = 1;
      ShowMatrix[0] = SetHour / 10; // Правим значения в матрице вывода
      ShowMatrix[1] = SetHour % 10;
      KeyLeftShort = 0;
    }
    else     
    if (KeyRightShort > 0){   // Кратко нажали правую клавишу
      SetMinute++;            // Увtkичиваем значение  минут
      if (SetMinute>59)
      {
        SetMinute = 0;
        SetHour++;
        if (SetHour>99) SetHour = 1;
        ShowMatrix[0] = SetHour / 10; // Правим значения в матрице вывода
        ShowMatrix[1] = SetHour % 10; 
      }
      ShowMatrix[2] = SetMinute / 10; // Правим значения в матрице вывода
      ShowMatrix[3] = SetMinute % 10; 
      KeyRightShort = 0;
    }
    else
    if (KeyModeState>LONG_PRESS) // Длительное нажатие клавиши MODE
    {
      TimerHour = NULL_TIME;                             // Выключаем таймер
      TimerMinute = NULL_TIME;   
      SystemState = SystemState & ~ ( TIMER_SET | TIMER_READY | TIMER_ON );  // Отключаем режим установки времени, бит срабатывания таймера и бит включения таймера
      SystemState = SystemState | TIMER_DISPLAY;          // Переводим в режим ТАЙМЕР
      KeyModeShort = 0;
      KeyModeState = 0;
      KeyLeftShort = 0;
      KeyRightShort = 0; 
    }    
  }  
  else // Обрабатываем режимы индикации     
  { 
  PrevSystemState = SystemState;
  if ((SystemState & CLOCK_DISPLAY) > 0)          // Режим ЧАСЫ
  {
    if (KeyModeState>LONG_PRESS){                 // Долго удерживаем клавишу Mode 
      SystemState = SystemState & ~ CLOCK_DISPLAY;  // Отключаем вывод текущего времени
      SystemState = SystemState | CLOCK_SET;      // Переводим в режим установки времени
      KeyModeState = 0;
      KeyModeShort = 0;
      KeyLeftShort = 0;
      KeyRightShort = 0;       
    }
    else
    if (KeyModeShort > 0){                        // Краткое нажатие на клавишу MODE
      SystemState = SystemState & ~ CLOCK_DISPLAY;  // Отключаем вывод текущего времени
      SystemState = SystemState | ALARM_DISPLAY;  // Переводим в режим БУДИЛЬНИК
      KeyModeShort = 0;
      KeyLeftShort = 0;
      KeyRightShort = 0;       
    }
    else     
    if (KeyLeftShort > 0 || KeyRightShort > 0){   // Кратко нажали левую или правую клавишу 
      SystemState = SystemState & ~ CLOCK_DISPLAY;  // Отключаем вывод текущего времени
      SystemState = SystemState | TIMER_SET;      // Переводим в режим установки таймера
      KeyModeState = 0;
      KeyLeftShort = 0;
      KeyRightShort = 0;
    }      
  }
  else
  if ((SystemState & ALARM_DISPLAY) > 0)          // Режим БУДИЛЬНИК
  { 
    if (KeyModeState>LONG_PRESS){                 // Долго удерживаем клавишу Mode 
       SystemState = SystemState & ~ ALARM_DISPLAY; // Отключаем вывод текущего времени
       SystemState = SystemState | ALARM_SET;     // Переводим в режим установки времени 
       KeyModeState = 0;
       KeyModeShort = 0;
       KeyLeftShort = 0;
       KeyRightShort = 0;        
    }
    else
    if (KeyModeShort > 0)                          // Краткое нажатие на клавишу MODE
    {
       SystemState = SystemState & ~ ALARM_DISPLAY;  // Отключаем вывод текущего времени
       SystemState = SystemState | TIMER_DISPLAY;  // Переводим в режим ТАЙМЕР
       KeyModeShort = 0;
       KeyLeftShort = 0;
       KeyRightShort = 0;        
    }
    else        
    if (KeyLeftShort > 0 || KeyRightShort > 0){   // Кратко нажали левую или правую клавишу 
       SystemState = SystemState & ~ ALARM_DISPLAY; // Отключаем вывод текущего времени
       SystemState = SystemState | TIMER_SET;     // Переводим в режим установки таймера
       KeyModeState = 0;
       KeyLeftShort = 0;
       KeyRightShort = 0;       
     }   
  }
  else
  if ((SystemState & TIMER_DISPLAY) > 0)          // Режим ТАЙМЕР  
  { 
    if (KeyModeState>LONG_PRESS){                 // Долго удерживаем клавишу Mode 
       SystemState = SystemState & ~ TIMER_DISPLAY; // Отключаем вывод установки таймера
       SystemState = SystemState | TIMER_SET;     // Переводим в режим установки таймера
       KeyModeState = 0;
       KeyModeShort = 0;
       KeyLeftShort = 0;
       KeyRightShort = 0;        
    }
    else
    if (KeyModeShort > 0){ // Краткое нажатие на клавишу MODE
        SystemState = SystemState & ~ TIMER_DISPLAY;  // Отключаем вывод таймера
        SystemState = SystemState | TEMP_DISPLAY;  // Переводим в режим ТЕРМОМЕТР
        KeyModeShort = 0;
        KeyLeftShort = 0;
        KeyRightShort = 0;         
    }      
  }
  else
  if ((SystemState & TEMP_DISPLAY) > 0)             // Режим Термометр
  {
    if (KeyModeShort > 0){                          // Краткое нажатие на клавишу MODE
       SystemState = SystemState & ~ TEMP_DISPLAY;    // Отключаем вывод температуры
       SystemState = SystemState | (CLOCK_DISPLAY | CLOCK_TIME);   // Переводим в режим ЧАСЫ вывод времени
       KeyModeShort = 0;
       KeyLeftShort = 0;
       KeyRightShort = 0;              
    }
    else        
    if (KeyLeftShort > 0 || KeyRightShort > 0){   // Кратко нажали левую или правую клавишу 
       SystemState = SystemState & ~ TEMP_DISPLAY;  // Отключаем вывод текущего времени
       SystemState = SystemState | TIMER_SET;     // Переводим в режим установки таймера
       KeyModeState = 0;
       KeyLeftShort = 0;
       KeyRightShort = 0;        
     }
   }  
  } 
 } 
}

void loop() // Основной цикл
{
  // Обрабатываем состояния клавиш
  if(bounceKEY_MODE.update()) // Изменилось сосотояние кнопки режима
  {
    IdleTimer = 0;
    if (bounceKEY_MODE.read()==0) // Кнопку режима нажали
    {
      KeyModeState = 1;
      KeyModeShort = 0;
    }
    else// Кнопку режима отпустили
    {
      if (KeyModeState>0 && KeyModeState<20) KeyModeShort = 1;
      KeyModeState = 0;
    }
  }
  
  if(bounceKEY_LEFT.update()) // Изменилось сосотояние левой кнопки
  {
    IdleTimer = 0;
    if (bounceKEY_LEFT.read()==0) // Левую кнопку нажали
    {
      KeyLeftState = 1;
      KeyLeftShort = 0;
    }
    else// Левую кнопку отпустили
    {
      if (KeyLeftState>0 && KeyLeftState<20) KeyLeftShort = 1;
      KeyLeftState = 0;
    }
  }
  
  if(bounceKEY_RIGHT.update()) // Изменилось сосотояние правой кнопки
  {
    IdleTimer = 0;
    if (bounceKEY_RIGHT.read()==0) // Правую кнопку нажали
    {
      KeyRightState = 1;
      KeyRightShort = 0;
    }
    else// Правую кнопку отпустили
    {      
      if (KeyRightState>0 && KeyRightState<20) KeyRightShort = 1;
      KeyRightState = 0;
    }  
  } 
}

void setup() // Инициализацция системы при включении питания или сбросе состояний
{
// Всякая шняга для отладки
//Serial.begin(9600);             // Инициализация COM порта на передачу данных (Для отладки)
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

SystemState = 0;                  // Иницализация таймера
SecCounter = 0;                   // Cброс счетчика секунд
Wire.begin();                     // Инициализация I2C

pinMode(13, OUTPUT);              // Инициализация пинов
pinMode(KEY_MODE, INPUT_PULLUP);    
pinMode(KEY_LEFT, INPUT_PULLUP);
pinMode(KEY_RIGHT, INPUT_PULLUP);
pinMode(BUZZER_PIN, OUTPUT);
pinMode(IND_CLOCK, OUTPUT);
pinMode(IND_ALARM, OUTPUT);
pinMode(IND_TIMER, OUTPUT);
pinMode(IND_TEMPRATURE, OUTPUT);
noTone(BUZZER_PIN);
digitalWrite(IND_CLOCK,0);
digitalWrite(IND_ALARM,0);
digitalWrite(IND_TIMER,0);
digitalWrite(IND_TEMPRATURE,0);

bounceKEY_MODE.attach(KEY_MODE);          // Инициализация объектов для предотвращения дребезга кнопок
bounceKEY_MODE.interval(BOUNCE_INTERVAL);
bounceKEY_LEFT.attach(KEY_LEFT);
bounceKEY_LEFT.interval(BOUNCE_INTERVAL);
bounceKEY_RIGHT.attach(KEY_RIGHT);
bounceKEY_RIGHT.interval(BOUNCE_INTERVAL);


tm1637.init();                    // Запуск индикатора
tm1637.set(BRIGHTNESS);
 
initDS3231();                     // Инициализация регистров DS3231 

attachInterrupt(0, blink, CHANGE);// Устанавливаем обработчик прерывания для мигания секундами по сигналу с DS3231 

TicksCount = 0;
Timer1.initialize(100000);        // Инициализуем таймер на 0.1 сек
Timer1.attachInterrupt(TimerTick); // Привязываем фукнцию к прерываниям таймера
Timer1.start();                   // Запускаем таймер

PrevSystemState = SystemState;            // Запоминаем предыдущее состояние системы                             
SystemState = CLOCK_DISPLAY | CLOCK_TIME; // Переключаемся в режим ЧАСЫ вывод времени
IdleTimer = 0;

//Инициализируем переменные для клавиш
KeyModeState = 0;   
KeyLeftState = 0;   
KeyRightState = 0;  
KeyModeShort = 0;   
KeyLeftShort = 0;   
KeyRightShort = 0;

// Инициализируем перменные срабатывания таймера и будильника.
AlarmHour = NULL_TIME;
AlarmMinute = NULL_TIME;
TimerHour = NULL_TIME;
TimerMinute = NULL_TIME;
}
