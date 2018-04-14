# http\_test

HTTP server load test tool

## Install

```
make
```

## Usage

```
./load_tester
  <ip>              \
  <port>                  \
  <num_concurrency>       \
  <total_connection_num>  \
  <num_thread>
```

## TODO

- Add progress bar

- enable `hostname` other than ip

## Known bugs

- `other response` contains those connections did not successfully transfer
`sizeof(GOOD_HEADER)` bytes (which may be 200 or 500)

- Threads may stuck at barrier when `num_thread` is large

