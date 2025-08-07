# ✅ Redis Server Testing Checklist

## 1. Connection
- [ ] Server accepts a TCP client connection
- [ ] Handles multiple clients simultaneously
- [ ] Gracefully closes disconnected clients

## 2. Commands
- [ ] `SET key value` stores the value correctly
- [ ] `GET key` retrieves the correct value
- [ ] `DEL key` removes the value
- [ ] Unknown commands return error
- [ ] Empty or malformed input returns error

## 3. Snapshot (RDB)
- [ ] `SAVE` creates a valid snapshot file
- [ ] Snapshot file can be loaded correctly on restart
- [ ] Snapshot handles concurrent commands safely
- [ ] Fails gracefully if disk is full or permission is denied

## 4. Data Integrity
- [ ] Values are retained across sessions using snapshot
- [ ] Binary file format is consistent
- [ ] No data loss during save/load

## 5. Concurrency
- [ ] Concurrent `SET`/`GET` operations do not conflict
- [ ] Snapshot during heavy load works correctly

## 6. Performance
- [ ] Handles 10,000+ operations/sec under light load
- [ ] Latency stays < 1ms for basic ops (GET, SET)

## 7. Robustness
- [ ] Handles invalid inputs without crashing
- [ ] Does not leak memory under load (`valgrind`, `ASan`)
- [ ] Restarts gracefully after crash

## 8. Edge Cases
- [ ] Handles empty keys or values
- [ ] Handles keys with special characters
- [ ] Handles large values (e.g. 1MB strings)

## 9. Logging
- [ ] Logs snapshot success/failure
- [ ] Logs unexpected behavior

## 10. Security
- [ ] Rejects command injection attempts
- [ ] Snapshot path cannot be tampered via input
