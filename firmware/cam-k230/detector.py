# detector — KPU YOLO person detection on the CanMV-K230, with a motion fallback.
#
# The K230's KPU runs a quantized YOLO .kmodel. CanMV exposes it via the `nncase`
# runtime / the `aidemo`-style `nn` API. If the model file is missing (or the API
# differs on your CanMV build) the detector degrades to a frame-difference motion
# gate so main.py still runs end-to-end. Real wiring is marked TODO(model).

try:
    import nncase_runtime as nn   # CanMV KPU runtime (name varies by image)
    _HAVE_KPU = True
except Exception:
    _HAVE_KPU = False

PERSON_CLASS = 0   # COCO "person"

class PersonDetector:
    def __init__(self, model_path):
        self.model_path = model_path
        self.ready = False
        self._prev = None
        if _HAVE_KPU:
            try:
                # TODO(model): load + build the kmodel KPU runtime here, e.g.
                #   self.kpu = nn.from_oj(open(model_path,'rb').read())
                #   self.kpu.set_input_tensor(...) etc.
                # Then detect() pre-processes the frame to the model input size,
                # runs the KPU, and parses YOLO boxes -> top person score.
                open(model_path, "rb").close()   # presence check only for now
                self.ready = False               # flip True once parsing is wired
            except OSError:
                self.ready = False

    def name(self):
        return "k230-kpu-yolo" if self.ready else "k230-motion-fallback"

    def detect(self, frame):
        """frame: an image object (image.Image). Returns (is_person, confidence)."""
        if self.ready:
            # TODO(model): run KPU + parse boxes; return top person box.
            return (False, 0.0)
        return self._motion(frame)

    def _motion(self, frame):
        # Coarse luma diff: sample a small grid from the frame buffer.
        try:
            buf = frame.bytearray()
        except Exception:
            return (False, 0.0)
        n = 32 * 24
        if len(buf) < n:
            return (False, 0.0)
        stride = len(buf) // n
        cur = bytes(buf[i * stride] for i in range(n))
        if self._prev is None:
            self._prev = cur
            return (False, 0.0)
        changed = sum(1 for a, b in zip(cur, self._prev) if abs(a - b) > 24)
        self._prev = cur
        frac = changed / float(n)
        # Reported as confidence; main maps a positive to kind="motion".
        return (False, frac if frac >= 0.04 else 0.0)
