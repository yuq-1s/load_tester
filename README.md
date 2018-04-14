# http\_test

HTTP server load test tool

## Install

```
make
```

## Usage

```
./load_tester
  <hostname>              \
  <port>                  \
  <num_concurrency>       \
  <total_connection_num>  \
  <num_thread>
```

## TODO

- add progress bar

- reuse sockaddr in `get_socket`

## Known bugs

- `other response` contains those connections did not successfully transfer
`sizeof(GOOD_HEADER)` bytes (which may be 200 or 500)

- threads may stuck at barrier when `num_thread` is large

