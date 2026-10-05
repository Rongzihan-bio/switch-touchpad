#!/usr/bin/env python3
"""Exercise the shipped StepTrackBar on the host, without Switch services/GPU."""
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
header = (ROOT/'third_party/libtesla/include/tesla.hpp').read_text()
start = header.index('class StepTrackBar : public TrackBar {')
opening = header.index('{',start)
depth = 1
end = opening+1
while depth:
    depth += (header[end] == '{') - (header[end] == '}')
    end += 1
assert header[end] == ';'
out = ROOT/'build/host'
out.mkdir(parents=True,exist_ok=True)
(out/'tesla-step-trackbar.hpp').write_text(
    '#include "tests/tesla_slider_runtime.hpp"\nnamespace tsl::elm {\n'+header[start:end+1]+'\n}\n')
subprocess.run([
    'c++','-std=c++20','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-parameter',
    '-fsanitize=address,undefined','-fno-omit-frame-pointer','-I.','-Ibuild/host',
    'tests/brightness_slider_tests.cpp','-o','build/host/brightness-slider-tests'
],cwd=ROOT,check=True)
subprocess.run([str(out/'brightness-slider-tests')],cwd=ROOT,check=True)
