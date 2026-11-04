# Синтаксис 

принцип языка то что всё что с маленькой буквы то это приватное а всё что с большой то это публичное к примеру 

```pgt
package main

fn Test() { // публичная функция
    var* test = 1 
}

fn test() { // приватная функция
    var* test = 1 
}

fn main() { // Точка входа в программу
    var* test = 1 
}

return 0 // Возвращает 0 из функции main
```

пакети как в go 
```pgt
package main

#use (
    "test"
)

однострочный импорт пакета
#use "test"
```

функции будут объявляться так

```pgt
fn main() {
    // код функции
}
```

а главний файл должен заканчиваться вот так 

```pgt
return 0
```

вот както так будеть выглядеть код на pgt language 2.0

```.pgt
package main

#use (
    "test"
)

fn main() {
    // код функции
}
return 0
```

а вот так будет выглядеть работа с переменными

```.pgt
первый вобор 

var* num = 0 
var* num = "Hello, World!"
var* num = true
var* num = 3.14

и можно вот так это второй выбор тут можно явно указать тип данных через + {тип}
но я рекомендую первый способ так как он более удобный и не требует лишнего кода

var* num + {int} = 10 // целое число
var* num + {string} = "Hello, World!" // строка
var* num + {bool} = true // булево значение
var* num + {float} = 3.14 // вещественное число


подробный разбор синтаксиса первого способа объявления переменной

var* // это ключевое слово для объявления переменной, оно указывает на то что переменная может быть изменяемой (mutable)
num // это имя переменной, оно может быть любым, но должно начинаться с буквы
= // это оператор присваивания, он используется для присвоения значения переменной
0 // это значение, которое присваивается переменной num, в данном случае это целое


подробный разбор синтаксиса второго способа объявления переменной

var* // это ключевое слово для объявления переменной, оно указывает на то что переменная может быть изменяемой (mutable)
num  // это имя переменной, оно может быть любым, но должно начинаться с буквы
+ {int} // это указание типа данных, в данном случае это целое число, можно указать string, bool, float
= // это оператор присваивания, он используется для присвоения значения переменной
10 // это значение, которое присваивается переменной num, в данном случае это целое число
```

вод и вывод и консоли будет такой

```.pgt
вод вот так 
input("Введите число: ", num)
а вывод вот так
output("Вы ввели число: ", num)
также можно 
print("Вы ввели число: ", num)
и 
println("Вы ввели число: ", num)
```

работа с массивами будет такой

```.pgt
var& := arr = [1, 2, 3, 4, 5] // массив целых чисел
```

циклы и условия будут такими

```.pgt
for i in arr {
    output(i)
}
if num > 0 {
    output("Число положительное")
} else {
    output("Число отрицательное")
}
цикл while будет таким образом
while num > 0 {
    num--
    output(num)
}
while num > 0 {
    num--
    output(num)
}

while num = 0 {
    num++
    output(num)
}

безконечно 
while true {
    // код
}
```

пример кода на pgt language 2.0 с использованием ввода и вывода

```.pgt
package main

#use (
    "test"
)

fn main() {
    var* num = 0
    input("Введите число: ", num)
    output("Вы ввели число: ", num)
    if num > 0 {
        output("Число положительное")
    } else {
        output("Число отрицательное")
    }
    while num > 0 {
        num--
        output(num)
    }
    while num = 0 {
        num++
        output(num)
    }
    
}
return 0
```


вызов функции будет таким

```pgt
package main

#use "test"

const nom = 1

fn test() {
    var* nom = 2
    output(nom)
}

fn main() {
    call& = ("test") // вызов функции
}
```


# modules



# Модули в PGT 2.0

## Разберёмся чуть поподробней как в PGT 2.0 устроенные модули

---

### Модули в PGT 2.0 устроены по принципу как в go есть 3 секции

<ul> 
    <li id="module">1. module</li>
    <li id="version">2. version</li>
    <li id="dependencies">3. dependencies</li>
</ul>

<h3 id="module">1. module</h3>

Первая секции описуеть названыя вашего проекта как в go 

```pgt.mod
module test-module-core // названыя вашего проекта как в go 
```

Указывается в самом начале файла pgt.mod и в нем может быть только одно название 

2. version

Вторая секции описуеть версию вашего проекта как в go 

```pgt.mod
    version 0.1
```

3. dependencies

Третья секции описуеть зависимости вашего проекта как в go 

---

## Что такое модуль?

```pgt.mod
module test-module-core // названыя вашего проекта как в go 

// версия компилятора можно узнать с помощью команды pgt --version будет что то токого 

// pgt --version
// PGT Compiler v0.1
// Built on Aprel 21 2026
// Author: pabla

version 0.1

// dependencies секция 

dependencies {
    "test_module" = "0.1"
    "test_module2" = "0.1"
    "test_module3" = "0.1"
    "test_module4" = "0.1"
    
}
```

### А теперь в используем в коде 

```pgt
package main

#use "test-module-core/test_module" // тут как в go 

fn main() {
    
}
```


# Константы

```pgt
package main

const PI = 3.14 // константа

// константы можно объявлять только в глобальной области видимости

// а вот как можно вызывать константу

fn main() {
    drawcall& = ("Pi")
}
```


# Ключевые слова языка

package - пакет

#use - импорт

fn - функция

var* - переменная

const - константа



if - если

else - иначе

while - пока

call& - вызов функции

for - для

input - ввод

output - вывод

print - вывод

println - вывод с новой строки

drawcall& - вызов константы

return 0 - выход из программы ОБЯЗАТЕЛЬНО В КОНЦЕ ПРОГРАММЫ В НЕ ВНУТРИ fn вот так

```main.pgt
package main

#use "test"

fn main() {
    
}

return 0
```

# Расширения языка

## Типы

`int` (32 бита), `int64`, `float`, `char`, `string`, `bool`, `void`, а также составные: `[]T` (массив), `*T` (указатель), имя структуры или интерфейса.

```pgt
var* a = 1                  // int
var* b int64 = 3000000000
var* c = 'x'                // char
var* d + {float} = 2        // явный тип
var* e int                  // нулевое значение
```

Преобразования: `int(x)`, `int64(x)`, `float(x)`, `char(x)`, `bool(x)`, `string(x)`.

## Операторы

Арифметика `+ - * / %`, сравнение `== != < > <= >=`, логика `&& || !` (с короткой оценкой), побитовые `& | ^ << >> ~`, составное присваивание `+= -= *= /= %=`, `++` и `--` для любых изменяемых значений (`arr[i]++`, `p.X--`).

Строки склеиваются через `+`, в том числе с числами и bool: `"n=" + 5`.

## Условия и циклы

```pgt
if x < 0 {
} else if x == 0 {
} else {
}

for var* i = 0; i < 10; i++ { }
for i in 0..10 { }          // 0..9
for x in arr { }
for i, x in arr { }
for ch in "text" { }
for cond { }
for { break }

switch x {
    case 1, 2 { }
    case 3 { }
    default { }
}
```

`break` и `continue` работают во всех циклах, `break` — и в `switch`.

## Массивы

```pgt
var* arr = [1, 2, 3]
arr[0] = 10
arr = append(arr, 4, 5)
println(len(arr), arr)
var* grid = make([]int, 5)
var* names + {[]string} = []
```

Выход за границы массива завершает программу с сообщением `panic: index out of range`.

## Структуры и методы

```pgt
struct Point {
    X int
    Y int
}

fn (p Point) Sum() int {        // получатель по значению
    return p.X + p.Y
}

fn (p *Point) Move(dx int) {    // получатель по указателю
    p.X += dx
}

var* p = Point{X: 1, Y: 2}
p.Move(5)
println(p, p.Sum())
```

## Указатели

```pgt
var* x = 1
var* px = &x
*px = 5
var* q = &Point{X: 1}
var* h = new(Point)
free(h)
```

`nil` — пустой указатель, строка, массив или интерфейс.

## Интерфейсы

Реализуются неявно, как в Go: достаточно, чтобы у типа были все методы.

```pgt
interface Shape {
    Area() float
}

fn describe(s Shape) {
    println(s.Area())
}
```

## Функции с типами

```pgt
fn add(a int, b int) int {
    return a + b
}

fn greet(name string) {
    println("Hello, " + name)
}
```

Параметры без типа считаются `int`. Функция без типа результата возвращает `int`, если в ней есть `return` со значением, иначе ничего.

## Ввод

```pgt
input("Число: ", n)        // int, int64, float, char, bool, string (одно слово)
inputln("Строка: ", line)  // строка целиком
```

## Встроенные функции

| Функция | Описание |
|---|---|
| `len(x)` | длина массива или строки |
| `append(a, v...)` | добавить элементы в массив |
| `make([]T, n)`, `new(T)`, `free(p)` | выделение и освобождение памяти |
| `str(x)`, `parseInt(s)`, `parseFloat(s)` | преобразование строк |
| `substr(s, from, n)`, `contains`, `indexOf`, `startsWith`, `endsWith`, `upper`, `lower` | строки |
| `abs`, `min`, `max`, `sqrt`, `pow`, `floor`, `ceil`, `sin`, `cos`, `tan`, `log`, `exp` | математика |
| `readFile(path)`, `writeFile(path, s)`, `appendFile(path, s)`, `fileExists(path)` | файлы |
| `rand()`, `srand(n)`, `exit(code)` | прочее |

Глобальные переменные и константы могут инициализироваться любым выражением, например вызовом функции.