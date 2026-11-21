# Стандартная библиотека PGT

Пакеты подключаются по имени: `#use "strings"`. Исходники написаны на PGT и лежат рядом с этим файлом;
низкоуровневые части (строки, файлы, время) реализованы в рантайме `runtime/pgt_runtime.c` и подключаются через `extern fn`.

Каталог библиотеки ищется так: переменная окружения `PGT_STD`, затем `../lib/pgt/std` рядом с компилятором, затем `std/` в исходниках.

## strings

Работа со строками. Строки неизменяемы: функции возвращают новую строку.

```pgt
#use "strings"

var* words = strings.Fields("  to be  or not ")   // [to be or not]
println(strings.Join(words, "-"))                 // to-be-or-not
println(strings.PadLeft("7", 3, '0'))             // 007

var* b strings.Builder
b.Write("Hello, ")
b.Write("PGT")
println(b.String())
```

| Функция | Описание |
|---|---|
| `Contains(s string, sub string) bool` | Есть ли sub в s |
| `ContainsChar(s string, c char) bool` | Есть ли символ c в s |
| `HasPrefix(s string, prefix string) bool` | Начинается ли s с prefix |
| `HasSuffix(s string, suffix string) bool` | Заканчивается ли s на suffix |
| `Index(s string, sub string) int` | Позиция первого вхождения sub или -1 |
| `IndexFrom(s string, sub string, from int) int` | Ищет sub, начиная с позиции from |
| `LastIndex(s string, sub string) int` | Позиция последнего вхождения sub или -1 |
| `IndexChar(s string, c char) int` | Позиция первого символа c или -1 |
| `Count(s string, sub string) int` | Возвращает число непересекающихся вхождений sub в s |
| `Upper(s string) string` | Все буквы заглавные |
| `Lower(s string) string` | Все буквы строчные |
| `Title(s string) string` | Делает заглавной первую букву каждого слова |
| `Repeat(s string, n int) string` | S, повторённая n раз |
| `Replace(s string, old string, with string) string` | Заменяет все вхождения old на with |
| `ReplaceN(s string, old string, with string, n int) string` | Заменяет не больше n первых вхождений |
| `Reverse(s string) string` | Строка задом наперёд |
| `EqualFold(a string, b string) bool` | Сравнивает строки без учёта регистра |
| `TrimSpace(s string) string` | Убирает пробелы, табуляции и переводы строк с обоих концов |
| `TrimLeft(s string) string` | Убирает пробельные символы слева |
| `TrimRight(s string) string` | Убирает пробельные символы справа |
| `Trim(s string, cutset string) string` | Убирает с обоих концов все символы, входящие в cutset |
| `TrimPrefix(s string, prefix string) string` | Убирает prefix в начале, если он есть |
| `TrimSuffix(s string, suffix string) string` | Убирает suffix в конце, если он есть |
| `PadLeft(s string, width int, fill char) string` | Дополняет слева символом fill до ширины width |
| `PadRight(s string, width int, fill char) string` | Дополняет справа символом fill до ширины width |
| `Center(s string, width int, fill char) string` | Выравнивает по центру в поле ширины width |
| `Split(s string, sep string) []string` | Делит s по разделителю sep. Пустой sep делит строку на отдельные символы |
| `Fields(s string) []string` | Делит строку по пробельным символам, пропуская пустые части |
| `Lines(s string) []string` | Делит текст на строки (понимает и `\n`, и `\r\n`) |
| `Join(parts []string, sep string) string` | Склеивает части через sep |
| `IsDigit(c char) bool` | Цифра 0–9 |
| `IsLetter(c char) bool` | Латинская буква |
| `IsUpper(c char) bool` | Заглавная латинская буква |
| `IsLower(c char) bool` | Строчная латинская буква |
| `IsAlnum(c char) bool` | Буква или цифра |
| `IsSpace(c char) bool` | Пробел, табуляция или перевод строки |
| `ToUpper(c char) char` | Символ в верхнем регистре |
| `ToLower(c char) char` | Символ в нижнем регистре |
| `struct Builder` | Собирает строку по частям: `var* b strings.Builder` |
| `(Builder) Write(s string)` | Добавляет строку |
| `(Builder) WriteChar(c char)` | Добавляет символ |
| `(Builder) WriteLine(s string)` | Добавляет строку и `\n` |
| `(Builder) Len() int` | Длина собранной строки |
| `(Builder) Reset()` | Очищает |
| `(Builder) String() string` | Готовая строка |

## math

Математика. `abs`, `min`, `max`, `sqrt`, `pow`, `floor`, `ceil`, `round`, `sin`, `cos`, `tan`, `log`, `exp` встроены в язык, здесь всё остальное.

```pgt
#use "math"

println(math.Pi, " ", math.Hypot(3, 4))    // 3.14159 5
println(math.Gcd(84, 36), " ", math.IsPrime(97))
```

Константы: `Pi`, `E`, `Sqrt2`, `Ln2`, `Ln10`, `MaxInt`, `MinInt`, `MaxInt64`, `MinInt64`.

| Функция | Описание |
|---|---|
| `Sqrt(x float) float` | Квадратный корень |
| `Cbrt(x float) float` | Кубический корень |
| `Pow(x float, y float) float` | X в степени y |
| `Exp(x float) float` | E в степени x |
| `Log(x float) float` | Натуральный логарифм |
| `Log2(x float) float` | Логарифм по основанию 2 |
| `Log10(x float) float` | Десятичный логарифм |
| `Hypot(x float, y float) float` | Длина гипотенузы √(x²+y²) |
| `Sin(x float) float` | Синус (радианы) |
| `Cos(x float) float` | Косинус |
| `Tan(x float) float` | Тангенс |
| `Asin(x float) float` | Арксинус |
| `Acos(x float) float` | Арккосинус |
| `Atan(x float) float` | Арктангенс |
| `Atan2(y float, x float) float` | Угол точки (x, y) в радианах |
| `Sinh(x float) float` | Гиперболический синус |
| `Cosh(x float) float` | Гиперболический косинус |
| `Tanh(x float) float` | Гиперболический тангенс |
| `Floor(x float) float` | Округление вниз |
| `Ceil(x float) float` | Округление вверх |
| `Round(x float) float` | Округление к ближайшему (половина — от нуля) |
| `Trunc(x float) float` | Отбрасывает дробную часть |
| `Abs(x float) float` | Модуль |
| `Mod(x float, y float) float` | Остаток от деления вещественных чисел (знак как у x) |
| `Min(a float, b float) float` | Меньшее из двух |
| `Max(a float, b float) float` | Большее из двух |
| `Clamp(x float, lo float, hi float) float` | Ограничивает x отрезком [lo, hi] |
| `Lerp(a float, b float, t float) float` | Линейная интерполяция между a и b |
| `Radians(deg float) float` | Градусы в радианы |
| `Degrees(rad float) float` | Радианы в градусы |
| `Inf() float` | Бесконечность |
| `NaN() float` | «не число» |
| `IsNaN(x float) bool` | Является ли x NaN |
| `IsInf(x float) bool` | Является ли x бесконечностью |
| `Sign(x float) int` | Возвращает -1, 0 или 1 |
| `ClampInt(x int, lo int, hi int) int` | Ограничивает целое отрезком [lo, hi] |
| `SignInt(x int) int` | Знак целого: -1, 0 или 1 |
| `PowInt(base int64, exp int) int64` | Возводит целое в неотрицательную степень (быстрое возведение) |
| `Gcd(a int, b int) int` | Наибольший общий делитель |
| `Lcm(a int, b int) int` | Наименьшее общее кратное |
| `IsPrime(n int) bool` | Простое ли число |
| `Factorial(n int) int64` | N! (до 20! включительно) |
| `Fib(n int) int64` | N-е число Фибоначчи (до 92) |
| `IsEven(n int) bool` | Чётное ли число |
| `IsOdd(n int) bool` | Нечётное ли число |
| `DigitSum(n int) int` | Сумма цифр числа |

## arrays

Функции для массивов. Массив передаётся по ссылке на данные, поэтому `Sort`, `Reverse` и `Fill` меняют сам массив.

```pgt
#use "arrays"

var* a = [5, 3, 9, 1]
arrays.Sort(a)                          // [1 3 5 9]
println(arrays.BinarySearch(a, 5))      // 2
```

| Функция | Описание |
|---|---|
| `Sum(a []int) int` | Сумма элементов |
| `Min(a []int) int` | Наименьший элемент (паника на пустом массиве) |
| `Max(a []int) int` | Наибольший элемент (паника на пустом массиве) |
| `Mean(a []int) float` | Среднее арифметическое |
| `Contains(a []int, v int) bool` | Есть ли v в массиве |
| `IndexOf(a []int, v int) int` | Индекс первого v или -1 |
| `Count(a []int, v int) int` | Сколько раз встречается v |
| `Equal(a []int, b []int) bool` | Одинаковы ли массивы поэлементно |
| `Copy(a []int) []int` | Независимая копия |
| `Slice(a []int, from int, to int) []int` | Возвращает копию элементов [from, to) |
| `Concat(a []int, b []int) []int` | Новый массив: a, затем b |
| `Range(from int, to int) []int` | Возвращает [from, from+1, ..., to-1] |
| `Repeat(v int, n int) []int` | Массив из n значений v |
| `Fill(a []int, v int)` | Заполняет массив значением v |
| `Reverse(a []int)` | Переворачивает массив на месте |
| `IsSorted(a []int) bool` | Отсортирован ли по возрастанию |
| `Sort(a []int)` | Сортирует массив по возрастанию (быстрая сортировка + вставки для малых кусков) |
| `SortDesc(a []int)` | Сортирует по убыванию |
| `BinarySearch(a []int, v int) int` | Ищет v в отсортированном массиве; -1, если не найден |
| `Unique(a []int) []int` | Возвращает элементы без повторов, сохраняя порядок первого появления |
| `SumFloat(a []float) float` | Сумма элементов |
| `MeanFloat(a []float) float` | Среднее арифметическое |
| `SortFloat(a []float)` | Сортирует по возрастанию |
| `ContainsString(a []string, v string) bool` | Есть ли строка v в массиве |
| `IndexOfString(a []string, v string) int` | Индекс первой строки v или -1 |
| `ReverseStrings(a []string)` | Переворачивает массив на месте |
| `SortStrings(a []string)` | Сортирует строки лексикографически (сортировка Шелла) |

## conv

Преобразования между строками и числами.

```pgt
#use "conv"

println(conv.ToHex(255), " ", conv.FormatFloat(2.0 / 3, 2))   // ff 0.67
if conv.IsInt(text) { var* n = conv.Atoi(text) }
```

| Функция | Описание |
|---|---|
| `Itoa(n int) string` | Число в строку |
| `Atoi(s string) int` | Строку в число (0, если не число) |
| `ParseInt64(s string) int64` | Строку в int64 |
| `ParseFloat(s string) float` | Строку в float |
| `ParseBool(s string) bool` | Понимает true/false, 1/0, yes/no |
| `IsInt(s string) bool` | Записано ли в строке целое число |
| `IsFloat(s string) bool` | Записано ли в строке число |
| `FormatFloat(x float, prec int) string` | Печатает число с фиксированным числом знаков после точки |
| `FormatInt(n int64, base int) string` | Записывает число в системе счисления base (2..36) |
| `ToBinary(n int64) string` | Двоичная запись |
| `ToHex(n int64) string` | Шестнадцатеричная запись |
| `ParseIntBase(s string, base int) int64` | Читает число в системе счисления base; неизвестные символы пропускаются |

## io

Построчное чтение стандартного ввода.

```pgt
#use "io"

var* line = io.ReadLine()
while !io.EOF() {
    println("> ", line)
    line = io.ReadLine()
}
```

| Функция | Описание |
|---|---|
| `ReadLine() string` | Читает следующую строку (без '\n'). В конце ввода возвращает "" и EOF() становится true |
| `EOF() bool` | Закончился ли ввод при последнем чтении |
| `ReadLines() []string` | Читает все оставшиеся строки |
| `Prompt(text string) string` | Печатает подсказку и читает строку целиком |
| `ReadInt(text string) int` | Печатает подсказку и читает целое |
| `ReadFloat(text string) float` | Печатает подсказку и читает число |

## os

Аргументы командной строки, окружение, файлы и запуск команд.

```pgt
#use "os"

for arg in os.Args() { println(arg) }
os.WriteFile("out.txt", "hello\n")
```

| Функция | Описание |
|---|---|
| `Args() []string` | Возвращает аргументы программы; Args()[0] — имя самой программы |
| `NumArgs() int` | Число аргументов без имени программы |
| `Arg(i int) string` | Возвращает i-й аргумент (начиная с 1) или "", если его нет |
| `Getenv(name string) string` | Значение переменной окружения или "" |
| `Run(cmd string) int` | Запускает команду оболочки и возвращает её код завершения |
| `Exit(code int)` | Завершает программу с кодом code |
| `ReadFile(path string) string` | Содержимое файла или "", если его нет |
| `WriteFile(path string, data string) bool` | Записывает файл заново |
| `AppendFile(path string, data string) bool` | Дописывает в конец файла |
| `Exists(path string) bool` | Существует ли файл или папка |
| `IsDir(path string) bool` | Является ли путь папкой |
| `Remove(path string) bool` | Удаляет файл |
| `ReadLines(path string) []string` | Читает файл и делит его на строки |

## time

Время и паузы. Моменты времени — секунды или миллисекунды Unix (`int64`).

```pgt
#use "time"

var* sw = time.StartStopwatch()
time.Sleep(100)
println(sw.ElapsedMs(), " ms; ", time.Format(time.Now(), time.DateTime))
```

Константы: `DateTime`, `Date`, `Clock`.

| Функция | Описание |
|---|---|
| `Now() int64` | Текущее время в секундах Unix |
| `NowMs() int64` | Текущее время в миллисекундах Unix |
| `Ticks() int64` | Монотонные миллисекунды: годятся для замеров, не зависят от смены часов |
| `Since(startTicks int64) int64` | Сколько миллисекунд прошло с момента Ticks() |
| `Sleep(ms int)` | Пауза в миллисекундах |
| `Format(seconds int64, layout string) string` | Форматирует время по шаблону strftime: time.Format(time.Now(), time.DateTime) |
| `Today() string` | Сегодняшняя дата в виде ГГГГ-ММ-ДД |
| `struct Stopwatch` | Секундомер |
| `StartStopwatch() Stopwatch` | Запускает секундомер |
| `(Stopwatch) ElapsedMs() int64` | Миллисекунды с запуска |
| `(Stopwatch) Restart()` | Начинает отсчёт заново |

## rand

Псевдослучайные числа (xorshift64*). Без `Seed` генератор стартует от текущего времени.

```pgt
#use "rand"

println(rand.Range(1, 6))       // бросок кубика
rand.Seed(42)                   // одна и та же последовательность при каждом запуске
```

| Функция | Описание |
|---|---|
| `Seed(seed int64)` | Задаёт начальное значение: одинаковый seed — одинаковые числа |
| `Int() int` | Возвращает неотрицательное случайное int |
| `IntN(n int) int` | Возвращает число от 0 до n-1 |
| `Range(lo int, hi int) int` | Возвращает число от lo до hi включительно |
| `Float() float` | Возвращает число из [0, 1) |
| `Bool() bool` | Случайный bool |
| `Chance(p float) bool` | Возвращает true с вероятностью p (от 0 до 1) |
| `Shuffle(a []int)` | Перемешивает массив |
| `ShuffleStrings(a []string)` | Перемешивает массив строк |
| `Pick(a []string) string` | Случайный элемент массива |

