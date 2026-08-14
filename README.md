# slipstream

A small binary format for sending market data and order messages, plus the tools
to read a CSV of quotes and trades and replay it over that format at the speed it
originally happened.

There are three parts:

- `slipstream::codec` turns messages into bytes and back. It handles streams
  where a message can be split across several reads.
- `slipstream::replay` reads the CSV file and turns rows into messages.
- `market_data` and `order_entry` are the two programs that replay the file.

Nothing here opens a socket. The programs write to standard output and read from
standard input, so you can pipe them together or save the output to a file.

## Build

You need CMake 3.25 or newer and a C++23 compiler. GCC 14 and Clang 17 both work.

```sh
cmake --preset debug
cmake --build --preset debug
```

Use `--preset release` instead for an optimised build.

## Run

Replay every quote in the sample file, as fast as the machine can go, and save
the result:

```sh
./build/debug/apps/market_data/market_data --file data/Quotes_and_Trades.csv --speed 0 > quotes.bin
```

Replay one symbol in real time, which takes about an hour because that is how
long the sample file covers:

```sh
./build/debug/apps/market_data/market_data --file data/Quotes_and_Trades.csv --symbol SYNTH1
```

Options:

| Option | What it does |
| --- | --- |
| `--file <path>` | The CSV to read. Required. |
| `--symbol <name>` | Only send rows for this symbol. Sends everything if left out. |
| `--speed <number>` | `1` is real time and the default. `2` is twice as fast. `0` sends everything immediately. |
| `--help` | Print the options. |

Messages go to standard output as raw bytes. Anything the program wants to tell
you goes to standard error, so redirecting the output never mixes the two.

`order_entry` is not finished yet. It will replay the trade rows as orders and
read the replies back.

## Tests

```sh
cd build/debug && ctest --output-on-failure
```

The tests cover the parts that are easy to get wrong and hard to notice: prices
converted without rounding, timestamps, messages arriving in pieces of any size,
and malformed input being rejected instead of crashing.

## The CSV file

`data/Quotes_and_Trades.csv` holds about an hour of made up market data for five
symbols. Lines starting with `#` are comments. Every row has the same nine
columns, and the `Type` column says which kind of row it is:

- `Q` is a quote, so it fills in the bid and ask columns.
- `T` is a trade, so it fills in the price and quantity columns.

Prices are written as decimals in the file and stored as whole numbers
internally, scaled by 10000. So `101.23` is stored as `1012300`. This avoids the
rounding you get from floating point, which matters when comparing prices for
equality.

## Layout

```
include/slipstream/    public headers
  codec/               the message format and how to read and write it
  replay/              reading the CSV
  error/               shared error handling
src/                   the code behind those headers
apps/                  the two programs
tests/                 the tests
data/                  the sample CSV
```

## Using a different compiler

The C++ version is set on the targets in this project, so it does not affect
anything else on your machine. If you want to build with a compiler other than
the default, make a `CMakeUserPresets.json` next to the existing presets file.
Git ignores it.

```json
{
    "version": 6,
    "configurePresets": [
        {
            "name": "local",
            "inherits": "debug",
            "cacheVariables": {
                "CMAKE_CXX_COMPILER": "/usr/bin/g++-14"
            }
        }
    ]
}
```
