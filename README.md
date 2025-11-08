# Mini Packet Gateway (PGW)

Упрощённая модель сетевого компонента PGW (Packet Gateway) для обработки UDP-запросов, управления сессиями абонентов по IMSI, ведения CDR-журнала и предоставления HTTP API.

<details>
<summary>(раскрыть)</summary>

## Цель проекта

Разработать упрощённую модель сетевого компонента PGW (Packet Gateway), способную обрабатывать UDP-запросы, управлять сессиями абонентов по IMSI, вести CDR-журнал, предоставлять HTTP API, поддерживать чёрный список IMSI и корректно завершать работу.

## Архитектура решения

**Основное приложение: pgw_server**
- Прием UDP пакетов, содержащих IMSI
- Создание сессии, если IMSI не в чёрном списке и сессия не существует
- Отправка ответа: 'created' или 'rejected'
- Запись CDR (IMSI, действие, время) в файл
- Удаление сессии по таймеру и запись CDR
- Чтение настроек из JSON-конфига: порты, таймаут, лимит offload, чёрный список и др.
- HTTP API: /check_subscriber и /stop
- Логирование всех ключевых действий
- Поддержка чёрного списка IMSI с отклонением запросов

**Тестовое приложение: pgw_client**
- Запуск из командной строки с IMSI
- Чтение настроек из JSON
- Отправка UDP-пакета с IMSI и получение ответа
- Вывод текста ответа
- Логирование отправки, ответа и ошибок

## Технические требования

- Основной код на C++17
- Сборка через CMake, все зависимости скачиваются автоматически
- Unit-тесты с использованием Google Test
- JSON-библиотека: nlohmann::json
- HTTP: cpp-httplib
- Логирование: spdlog
- Сокеты: Berkeley Sockets

</details>

##  Описание

Mini PGW - это упрощённая реализация Packet Gateway, которая:
- Принимает UDP-запросы с IMSI в BCD-кодировке
- Управляет сессиями абонентов с автоматическим таймаутом
- Ведёт CDR-журнал всех операций
- Предоставляет HTTP API для проверки статуса абонентов
- Поддерживает чёрный список IMSI
- Реализует graceful shutdown с постепенным завершением сессий

##  Структура проекта

```
mini-pgw/
├── src/
│   ├── common/          # Общие утилиты (кодирование IMSI в BCD)
│   ├── server/          # Сервер (UDP + HTTP API)
│   └── client/          # UDP клиент для тестирования
├── tests/               # Unit-тесты
├── configs/            # Примеры конфигурационных файлов
├── build.sh            # Скрипт сборки проекта
├── test.sh             # Скрипт запуска тестов
├── test_coverage.sh    # Скрипт запуска тестов с coverage
└── CMakeLists.txt      # CMake конфигурация
```

## Быстрый старт

### Сборка проекта

**С помощью скрипта:**
```bash
./build.sh
```

**Вручную:**
```bash
mkdir build
cd build
cmake ..
cmake --build . -j$(nproc)
```

### Проверка сборки

После сборки в директории `build/` будут созданы исполняемые файлы:
- `build/src/server/pgw_server` - сервер
- `build/src/client/pgw_client` - клиент

##  Тестирование

### Запуск тестов

**С помощью скрипта:**
```bash
./test.sh
```

**Вручную:**
```bash
cd build
ctest --output-on-failure
```

### Coverage:
`server.cpp`:
```
Summary coverage rate:
  lines......: 71.3% (206 of 289 lines)
  functions..: 87.0% (20 of 23 functions)
  branches...: 42.6% (206 of 484 branches)
```
`imsi_to_bcd.cpp`: 100%


## HTTP API


### GET /health
Проверка работоспособности сервера.

**Пример:**
```bash
curl http://localhost:8080/health
# Ответ: ok
```

### GET /check_subscriber?imsi=<IMSI>
Проверка статуса абонента.

**Параметры:**
- `imsi` (обязательный) - IMSI абонента

**Ответы:**
- `200 OK` с телом `active` - сессия активна
- `200 OK` с телом `not active` - сессия не активна
- `400 Bad Request` - отсутствует параметр imsi

**Пример:**
```bash
curl "http://localhost:8080/check_subscriber?imsi=123456123456788"
# Ответ: active или not active
```

### POST /stop
Graceful shutdown сервера. Завершает работу с постепенным удалением сессий.

**Параметры (опционально):**
- `rate` - скорость удаления сессий в секунду (по умолчанию из конфига)

**Пример:**
```bash
curl -X POST -d '' "http://localhost:8080/stop?rate=5"
# Ответ: offload_started
```

## Конфигурационные файлы

### Сервер (configs/pgw_server_conf.json)

```json
{
  "udp_ip": "0.0.0.0",
  "udp_port": 9000,
  "session_timeout_sec": 30,
  "cdr_file": "cdr.log",
  "http_port": 8080,
  "graceful_shutdown_rate": 10,
  "log_file": "server.log",
  "log_level": "info",
  "blacklist": [
    "123456123456789",
    "111111111111111"
  ]
}
```

**Параметры:**
- `udp_ip` - IP адрес для UDP сервера (0.0.0.0 = все интерфейсы)
- `udp_port` - порт UDP сервера
- `session_timeout_sec` - таймаут сессии в секундах
- `cdr_file` - путь к файлу CDR журнала
- `http_port` - порт HTTP API
- `graceful_shutdown_rate` - скорость graceful shutdown (сессий в секунду)
- `log_file` - путь к файлу логов
- `log_level` - уровень логирования (debug, info, warn, error)
- `blacklist` - массив IMSI в чёрном списке

### Клиент (configs/pgw_client_conf.json)

```json
{
  "server_ip": "127.0.0.1",
  "server_port": 9000,
  "log_file": "client.log",
  "log_level": "info",
  "tx_timeout_ms": 2000
}
```

**Параметры:**
- `server_ip` - IP адрес сервера
- `server_port` - порт сервера
- `log_file` - путь к файлу логов
- `log_level` - уровень логирования
- `tx_timeout_ms` - таймаут ожидания ответа в миллисекундах

##  Использование

### Запуск сервера

```bash
cd build
./src/server/pgw_server [путь_к_конфигу]
# По умолчанию: configs/pgw_server_conf.json
```

### Запуск клиента

```bash
cd build
./src/client/pgw_client <IMSI> [путь_к_конфигу]
# Пример: ./src/client/pgw_client 
```

### Пример работы

1. Запустите сервер:
```bash
./build/src/server/pgw_server
```

2. В другом терминале отправьте запрос через клиент:
```bash
./build/src/client/pgw_client 123456789012345
# Ответ: created
```

3. Проверьте статус через HTTP API:
```bash
curl "http://localhost:8080/check_subscriber?imsi=123456789012345"
# Ответ: active
```

4. Проверьте CDR журнал:
```bash
cat cdr.log
# Формат: timestamp, IMSI, action
# 2025-11-08T15:36:42+0300, 1021021021, created
# 2025-11-08T15:37:12+0300, 1021021021, timeout
```

## UML Диаграмма

![alt tag](https://github.com/byoverr/mini-pgw/blob/main/img/uml.png "UML")

**Структура классов:**
- `Server` - основной класс сервера (UDP + HTTP)
- `Config` - структура конфигурации
- `imsi_to_bcd` - утилиты кодирования/декодирования IMSI


## Postman Collection

Postman collection для тестирования HTTP API находится в файле `postman_collection.json`.

- **Health Check** - проверка работоспособности
- **Check Subscriber - Active** - проверка активного абонента
- **Check Subscriber - Not Active** - проверка несуществующего абонента
- **Check Subscriber - Missing Param** - проверка ошибки 400
- **Graceful Shutdown** - остановка сервера

**Настройка переменных:**
- `base_url` - базовый URL (по умолчанию: `http://localhost:8080`)

## Требования

- CMake 3.16+
- C++17 компилятор (GCC 7+, Clang 5+)

