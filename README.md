# TSXGuard
A header only thread synchronization library using Intel RTM (Restricted Transactional Memory)

## Example Use
```c++

// a global lock
SpinLock lock;

// initialize a variable to capture
// user aborts
unsigned char status = 0;

const int n_retries = 20;
{
  TSX::TSXGuard guard(n_retries,lock,status);

  // everything in scope will execute
  // atomically using Intel TSX

}
```
