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
| `Contains(s string, sub string) bool` |  |
| `ContainsChar(s string, c char) bool` |  |
| `HasPrefix(s string, prefix string) bool` |  |
| `HasSuffix(s string, suffix string) bool` |  |
| `Index(s string, sub string) int` |  |
| `IndexFrom(s string, sub string, from int) int` | Index ищет sub, начиная с позиции from. |
| `LastIndex(s string, sub string) int` |  |
| `IndexChar(s string, c char) int` |  |
| `Count(s string, sub string) int` | возвращает число непересекающихся вхождений sub в s. |
| `Upper(s string) string` |  |
| `Lower(s string) string` |  |
| `Title(s string) string` | делает заглавной первую букву каждого слова. |
| `Repeat(s string, n int) string` |  |
| `Replace(s string, old string, with string) string` |  |
| `ReplaceN(s string, old string, with string, n int) string` | заменяет не больше n первых вхождений. |
| `Reverse(s string) string` |  |
| `EqualFold(a string, b string) bool` | сравнивает строки без учёта регистра. |
| `TrimSpace(s string) string` |  |
| `TrimLeft(s string) string` |  |
| `TrimRight(s string) string` |  |
| `Trim(s string, cutset string) string` | убирает с обоих концов все символы, входящие в cutset. |
| `TrimPrefix(s string, prefix string) string` |  |
| `TrimSuffix(s string, suffix string) string` |  |
| `PadLeft(s string, width int, fill char) string` |  |
| `PadRight(s string, width int, fill char) string` |  |
| `Center(s string, width int, fill char) string` |  |
| `Split(s string, sep string) []string` | делит s по разделителю sep. Пустой sep делит строку на отдельные символы. |
| `Fields(s string) []string` | делит строку по пробельным символам, пропуская пустые части. |
| `Lines(s string) []string` |  |
| `Join(parts []string, sep string) string` |  |
| `IsDigit(c char) bool` |  |
| `IsLetter(c char) bool` |  |
| `IsUpper(c char) bool` |  |
| `IsLower(c char) bool` |  |
| `IsAlnum(c char) bool` |  |
| `IsSpace(c char) bool` |  |
| `ToUpper(c char) char` |  |
| `ToLower(c char) char` |  |
| `struct Builder` |  |
| `(Builder) Write(s string)` |  |
| `(Builder) WriteChar(c char)` |  |
| `(Builder) WriteLine(s string)` |  |
| `(Builder) Len() int` |  |
| `(Builder) Reset()` |  |
| `(Builder) String() string` |  |

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
| `Sqrt(x float) float` |  |
| `Cbrt(x float) float` |  |
| `Pow(x float, y float) float` |  |
| `Exp(x float) float` |  |
| `Log(x float) float` |  |
| `Log2(x float) float` |  |
| `Log10(x float) float` |  |
| `Hypot(x float, y float) float` |  |
| `Sin(x float) float` |  |
| `Cos(x float) float` |  |
| `Tan(x float) float` |  |
| `Asin(x float) float` |  |
| `Acos(x float) float` |  |
| `Atan(x float) float` |  |
| `Atan2(y float, x float) float` |  |
| `Sinh(x float) float` |  |
| `Cosh(x float) float` |  |
| `Tanh(x float) float` |  |
| `Floor(x float) float` |  |
| `Ceil(x float) float` |  |
| `Round(x float) float` |  |
| `Trunc(x float) float` |  |
| `Abs(x float) float` |  |
| `Mod(x float, y float) float` | — остаток от деления вещественных чисел (знак как у x). |
| `Min(a float, b float) float` |  |
| `Max(a float, b float) float` |  |
| `Clamp(x float, lo float, hi float) float` |  |
| `Lerp(a float, b float, t float) float` |  |
| `Radians(deg float) float` |  |
| `Degrees(rad float) float` |  |
| `Inf() float` |  |
| `NaN() float` |  |
| `IsNaN(x float) bool` |  |
| `IsInf(x float) bool` |  |
| `Sign(x float) int` | возвращает -1, 0 или 1. |
| `ClampInt(x int, lo int, hi int) int` |  |
| `SignInt(x int) int` |  |
| `PowInt(base int64, exp int) int64` | возводит целое в неотрицательную степень (быстрое возведение). |
| `Gcd(a int, b int) int` |  |
| `Lcm(a int, b int) int` |  |
| `IsPrime(n int) bool` |  |
| `Factorial(n int) int64` |  |
| `Fib(n int) int64` |  |
| `IsEven(n int) bool` |  |
| `IsOdd(n int) bool` |  |
| `DigitSum(n int) int` | Digits возвращает сумму цифр числа. |

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
| `Sum(a []int) int` |  |
| `Min(a []int) int` |  |
| `Max(a []int) int` |  |
| `Mean(a []int) float` |  |
| `Contains(a []int, v int) bool` |  |
| `IndexOf(a []int, v int) int` |  |
| `Count(a []int, v int) int` |  |
| `Equal(a []int, b []int) bool` |  |
| `Copy(a []int) []int` |  |
| `Slice(a []int, from int, to int) []int` | возвращает копию элементов [from, to). |
| `Concat(a []int, b []int) []int` |  |
| `Range(from int, to int) []int` | возвращает [from, from+1, ..., to-1]. |
| `Repeat(v int, n int) []int` |  |
| `Fill(a []int, v int)` |  |
| `Reverse(a []int)` |  |
| `IsSorted(a []int) bool` |  |
| `Sort(a []int)` | сортирует массив по возрастанию (быстрая сортировка + вставки для малых кусков). |
| `SortDesc(a []int)` |  |
| `BinarySearch(a []int, v int) int` | ищет v в отсортированном массиве; -1, если не найден. |
| `Unique(a []int) []int` | возвращает элементы без повторов, сохраняя порядок первого появления. |
| `SumFloat(a []float) float` |  |
| `MeanFloat(a []float) float` |  |
| `SortFloat(a []float)` |  |
| `ContainsString(a []string, v string) bool` |  |
| `IndexOfString(a []string, v string) int` |  |
| `ReverseStrings(a []string)` |  |
| `SortStrings(a []string)` | сортирует строки лексикографически (сортировка Шелла). |

## conv

Преобразования между строками и числами.

```pgt
#use "conv"

println(conv.ToHex(255), " ", conv.FormatFloat(2.0 / 3, 2))   // ff 0.67
if conv.IsInt(text) { var* n = conv.Atoi(text) }
```

| Функция | Описание |
|---|---|
| `Itoa(n int) string` |  |
| `Atoi(s string) int` |  |
| `ParseInt64(s string) int64` |  |
| `ParseFloat(s string) float` |  |
| `ParseBool(s string) bool` | понимает true/false, 1/0, yes/no. |
| `IsInt(s string) bool` |  |
| `IsFloat(s string) bool` |  |
| `FormatFloat(x float, prec int) string` | печатает число с фиксированным числом знаков после точки. |
| `FormatInt(n int64, base int) string` | записывает число в системе счисления base (2..36). |
| `ToBinary(n int64) string` |  |
| `ToHex(n int64) string` |  |
| `ParseIntBase(s string, base int) int64` | читает число в системе счисления base; неизвестные символы пропускаются. |

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
| `ReadLine() string` | читает следующую строку (без '\n'). В конце ввода возвращает "" и EOF() становится true. |
| `EOF() bool` |  |
| `ReadLines() []string` | читает все оставшиеся строки. |
| `Prompt(text string) string` | печатает подсказку и читает строку целиком. |
| `ReadInt(text string) int` |  |
| `ReadFloat(text string) float` |  |

## os

Аргументы командной строки, окружение, файлы и запуск команд.

```pgt
#use "os"

for arg in os.Args() { println(arg) }
os.WriteFile("out.txt", "hello\n")
```

| Функция | Описание |
|---|---|
| `Args() []string` | возвращает аргументы программы; Args()[0] — имя самой программы. |
| `NumArgs() int` |  |
| `Arg(i int) string` | возвращает i-й аргумент (начиная с 1) или "", если его нет. |
| `Getenv(name string) string` |  |
| `Run(cmd string) int` | запускает команду оболочки и возвращает её код завершения. |
| `Exit(code int)` |  |
| `ReadFile(path string) string` |  |
| `WriteFile(path string, data string) bool` |  |
| `AppendFile(path string, data string) bool` |  |
| `Exists(path string) bool` |  |
| `IsDir(path string) bool` |  |
| `Remove(path string) bool` |  |
| `ReadLines(path string) []string` | читает файл и делит его на строки. |

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
| `Now() int64` | — текущее время в секундах Unix. |
| `NowMs() int64` | — текущее время в миллисекундах Unix. |
| `Ticks() int64` | — монотонные миллисекунды: годятся для замеров, не зависят от смены часов. |
| `Since(startTicks int64) int64` |  |
| `Sleep(ms int)` |  |
| `Format(seconds int64, layout string) string` | форматирует время по шаблону strftime: time.Format(time.Now(), time.DateTime). |
| `Today() string` |  |
| `struct Stopwatch` | Stopwatch измеряет прошедшее время. |
| `StartStopwatch() Stopwatch` |  |
| `(Stopwatch) ElapsedMs() int64` |  |
| `(Stopwatch) Restart()` |  |

## rand

Псевдослучайные числа (xorshift64*). Без `Seed` генератор стартует от текущего времени.

```pgt
#use "rand"

println(rand.Range(1, 6))       // бросок кубика
rand.Seed(42)                   // одна и та же последовательность при каждом запуске
```

| Функция | Описание |
|---|---|
| `Seed(seed int64)` |  |
| `Int() int` | возвращает неотрицательное случайное int. |
| `IntN(n int) int` | возвращает число от 0 до n-1. |
| `Range(lo int, hi int) int` | возвращает число от lo до hi включительно. |
| `Float() float` | возвращает число из [0, 1). |
| `Bool() bool` |  |
| `Chance(p float) bool` | возвращает true с вероятностью p (от 0 до 1). |
| `Shuffle(a []int)` |  |
| `ShuffleStrings(a []string)` |  |
| `Pick(a []string) string` |  |
