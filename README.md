# SC_JSFX

Evaluates [JSFX](https://www.reaper.fm/sdk/js/js.php) code within SuperCollider by exposing a scriptable [EEL2 VM](https://www.cockos.com/EEL2/) within a UGen.
This is still a WIP.

## Build

Currently only tested for macOS.
Should work for Linux, but probably not for Windows right now.

```shell
git clone --recursive https://github.com/capital-G/SC_JSFX.git

# replace DSC_SRC_PATH w/ your local source code copy of SuperCollider
# and adjust the CMAKE_INSTALL_PREFIX if necessary
cmake \
    -S . \
    -B build \
    -DSC_SRC_PATH=/Users/scheiba/github/supercollider \
    -DCMAKE_INSTALL_PREFIX=./install
cmake --build build --config Release
cmake --install build --config Release
```

Symlink or copy the content of the `install` folder to `Platform.userExtensionDir`.

## Demo

Scripts are registered on the server via `JSFXDef` and behaves like `SynthDef`.

All inputs are available via the variables `in0`, `in1`, ... and the outputs need to be written to `out0`, `out1`, ...

### In = Output * 0.5

```supercollider
// start the server
s.boot;

// registers the script on the server with identifier \simple
// like on SynthDef, remember to call .add
~simple = JSFXDef(\simple, "out0 = in0 * 0.5;").add;

// spawn a synth which evaluates our script
(
Ndef(\x, {JSFX.ar(
	1, // numOutputs
	~simple, // script to use - can also be JSFXDef(\simple) or \simple
	SinOsc.ar(200.0)), // ... the inputs to the script
}).scope;
)
```

### Modulate parameters

```supercollider
~modulate = JSFXDef(\modulate, "out0 = in0 * in1;").add;

Ndef(\x, {JSFX.ar(1, ~modulate, SinOscFB.ar(200.0, 1.3), LFPulse.ar(5.2, width: 0.2)) * 0.2}).play;
```

### Single sample feedback

#### One pole filter

```supercollider
(
~onePoleFilter = JSFXDef(\onePoleFilter, "
y1 += 0; // make the variable persistent across runs
alpha = 0.95;
// the one pole filter formula
out0 = alpha * y1 + (1.0 - alpha) * in0;
y1 = out0; // write value to history
").add;
)

Ndef(\x, {JSFX.ar(1, ~onePoleFilter, Saw.ar(400.0)) * 0.2}).play;
```

#### Feedback SinOsc

A phase modulatable `SinOscFB`

```supercollider
(
~sinOscFB = JSFXDef(\sinOscFB, "
phase += 0;
y1 += 0;
twoPi = 2*$pi;
inc += 0;

inc = twoPi * in0 / srate;

x = phase + (in1 * y1) + in2;

phase += inc;
// wrap phase
phase -= (phase >= twoPi) * twoPi;

out0 = sin(x);

y1 = out0;
").add;
)

(
Ndef(\x, {
	var sig = JSFX.ar(1, ~sinOscFB,
		\freq.ar(100.0), // in0 = freq
		\fb.ar(0.6, spec: [0.0, pi]), // in1 = fb
		SinOsc.ar(\phaseModFreq.ar(1000.0 * pi)) * \modAmt.ar(0.0, spec:[0.0, 1000.0]),  // in2 = phaseMod
	);
	sig * 0.1;
}).play.gui;
)
```

#### Delay line

Write sample accurate into a modulatable delay line.

```supercollider
(
~delayLine = JSFXDef(\delayLine, "
buf[in1] = in0;
out0 = buf[in2];
").add;
)

(
Ndef(\x, {
	var bufSize = SinOsc.ar(4.2).range(1000, 2000);
	var writePos = LFSaw.ar(2.0, 0.02).range(1, bufSize);
	var readPos = LFSaw.ar(pi, 0.0).range(1, bufSize);
	var sig = JSFX.ar(1, ~delayLine,
		SinOsc.ar(100.0),
		writePos.floor,
		readPos.floor,
	);
	sig.dup * 0.1;
}).play;
)
```

#### Complex oscillator

Two cross phase-modulated sine oscillators, 64 times oversampled.

```supercollider
(
~complex = JSFXDef(\complex, "
twopi = 2*$pi;

phaseA += 0;
phaseB += 0;

freqA = in0;
freqB = in1;
modIndexA = in2;
modIndexB = in3;

oversample = 64;

osSrate = srate * oversample;
incA = freqA / osSrate;
incB = freqB / osSrate;

sumA = 0;
sumB = 0;

// calculate subsaples
loop(oversample,
    phaseA += incA;
    phaseB += incB;
    // wrap phases between [0, 1)
    phaseA -= floor(phaseA);
    phaseB -= floor(phaseB);

    // apply cross-phase modulation
    phaseA = phaseA + modIndexA * sin(twopi * phaseB);
    phaseB = phaseB + modIndexB * sin(twopi * phaseA);

    // accumulate (for downsampling)
    sumA += sin(twopi * phaseA);
    sumB += sin(twopi * phaseB);
);

// scale down b/c of os
out0 = sumA / oversample;
out1 = sumB / oversample;
").add;
)

(
Ndef(\y, {
	var sig = JSFX.ar(2, ~complex, 
		\freqA.ar(200.0),
		\freqB.ar(pi*100),
		\modA.ar(0.02, spec: [-0.1, 0.1]) * 0.05 * Env.perc(releaseTime: \releaseTime.kr(0.2)).ar(gate: Impulse.ar(\offsetKick.kr(4.0))),
		\modB.ar(0.0, spec: [-0.1, 0.1]) * 0.05,
	);
	sig * 0.1;
}).play.gui;
)
```

### Multi-channel

```supercollider
~multi = JSFXDef(\multi, "out0 = in0 * in1; out1 = in0 * in2").add;

(
Ndef(\y, {JSFX.ar(2, ~multi, 
	SinOscFB.ar(200.0, 1.3), // in0
	LFPulse.ar(5.2, width: 0.2), // in1
	LFPulse.ar(3.2, width: 0.3) // in2
) * 0.2}).play;
)
```


## ToDo

Currently not all features of JSFX are available as currently only the EEL2 VM is exposed and can be scripted.

* [ ] Write Help file
* [ ] Allow for live-coding of JSFX scripts
* [ ] kr version?
* [ ] Turn on compiler optimization for platforms
* [ ] Expose sliders?
* [ ] Use WDL GUI?
* [ ] Have support with existing JSFX plugins?

## License

EEL2 and WDL by *Cockos* are BSD licensed.

This project is GPL-3.0 licensed.
