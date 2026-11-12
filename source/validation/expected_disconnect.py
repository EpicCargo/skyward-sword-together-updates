"""Bounded fixture intent for an expected host EOF; never gameplay acceptance."""
import math


class ExpectedDisconnect:
    """An explicit pre-EOF request cannot extend its own observation deadline."""

    def __init__(self):
        self.request = None
        self.seconds = None
        self.armed_at = None
        self.deadline = None
        self.disconnected_at = None

    @staticmethod
    def _time(now):
        if isinstance(now, bool) or not isinstance(now, (int, float)) or not math.isfinite(now) or now < 0:
            raise ValueError('Expected a finite nonnegative monotonic time')
        return now

    def observe(self, intent, now):
        now = self._time(now)
        if intent is None:
            return
        if (not isinstance(intent, dict) or set(intent) != {'kind', 'request', 'seconds'} or
                intent['kind'] != 'native-own-disconnect' or
                type(intent['request']) is not int or not 0 < intent['request'] < 2**31 or
                type(intent['seconds']) is not int or not 1 <= intent['seconds'] <= 90):
            raise ValueError('Invalid explicit native-disconnect observation intent')
        if self.request is not None:
            if intent['request'] != self.request or intent['seconds'] != self.seconds:
                raise ValueError('A disconnect observation request cannot be replaced')
            return
        if self.disconnected_at is not None:
            raise ValueError('Disconnect cannot be authorized after EOF')
        self.request = intent['request']
        self.seconds = intent['seconds']
        self.armed_at = now
        self.deadline = now + self.seconds

    def eof(self, now):
        now = self._time(now)
        if self.disconnected_at is not None:
            raise RuntimeError('Duplicate host EOF')
        if self.deadline is None or not self.armed_at <= now < self.deadline:
            raise RuntimeError('Unexpected or expired host EOF')
        self.disconnected_at = now

    def waiting(self, now, *, final_capture):
        now = self._time(now)
        if type(final_capture) is not bool or self.disconnected_at is None:
            raise RuntimeError('An observed host EOF is required')
        if not self.disconnected_at <= now < self.deadline:
            raise RuntimeError('Expected-disconnect observation timed out')
        return not final_capture

    def status(self):
        return dict(request=self.request, armed_at=self.armed_at, deadline=self.deadline,
                    host_eof_at=self.disconnected_at, gameplay_accepted=False)
