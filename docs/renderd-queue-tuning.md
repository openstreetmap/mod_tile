# renderd Queue Tuning

`renderd` keeps incoming render requests in five queues:

- priority request queue: missing tiles where a client is waiting
- request queue: stale tiles where a client is waiting
- low priority request queue: less urgent stale/style refresh work
- dirty queue: background work where no client waits for the response
- bulk request queue: explicit bulk rendering work

Requests are fetched in that priority order. When a time-critical request queue
is full, new requests overflow into the dirty queue. That prevents immediate
drops, but it also means the overflowed request no longer gets client-waiting
priority.

## Configuration

Set the queue limits in the active `[renderd]` section of `renderd.conf`:

```ini
[renderd]
num_threads=4
request_queue_limit=256
dirty_queue_limit=8000
```

`request_queue_limit` applies separately to each of the priority, normal, low
priority, and bulk queues. `dirty_queue_limit` applies to the background dirty
queue. If the dirty queue is also full, new overflow work is dropped.

The defaults keep the previous compiled-in behavior:

- with metatiles: `request_queue_limit=256`, `dirty_queue_limit=8000`
- without metatiles: `request_queue_limit=512`, `dirty_queue_limit=10000`

## Operating Guidance

Increase `dirty_queue_limit` when dirty tiles are dropped during peak load but
the server has quiet periods later where it can catch up. This is useful for
large public tile services where preserving expired work can improve freshness
after the peak passes.

Do not increase `dirty_queue_limit` blindly on an overloaded server. If the
higher-priority queues are continuously non-empty, dirty work may still starve,
and a larger dirty queue mainly stores more backlog. Watch queue length, queue
time, render throughput, and dropped-tile metrics before and after the change.

Increase gradually. For example, move from the default to a limit sized for
roughly 10 minutes of dirty work, then one hour, before trying day-scale values.
The right value depends on metatile size, render throughput, expiry volume, and
whether the service has predictable low-load windows.

## Rollout Checklist

1. Record current `renderd_queue`, `renderd_queue_time`, and
   `renderd_processed` Munin graphs.
2. Set `dirty_queue_limit` in `renderd.conf`.
3. Restart `renderd` during a maintenance window.
4. Confirm startup logs show the intended `request_queue_limit` and
   `dirty_queue_limit`.
5. Watch whether dropped dirty work decreases without sustained growth in queue
   time.
6. Roll back to the previous values if dirty queue time grows continuously or
   missing-tile requests appear to be delayed by old background work.

This setting only changes queue capacity. It does not implement bounded
overtaking or fairness between queues; those would require a larger queue
scheduler change.
