import sys, time
from datetime import datetime
from canloader import cli
original = cli._Progress.__call__
def paced_progress(self, committed, total):
    original(self, committed, total)
    if committed == 1000:
        print("\nFAULT_WINDOW_OPEN at=" + datetime.now().astimezone().isoformat() + " progress=1000/" + str(total), flush=True)
    if 0 < committed < total:
        time.sleep(0.25)
cli._Progress.__call__ = paced_progress
raise SystemExit(cli.main())
