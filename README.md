# SC_JSFX

Evaluates [JSFX](https://www.reaper.fm/sdk/js/js.php) code within SuperCollider by exposing a scriptable [EEL2 VM](https://www.cockos.com/EEL2/) within a UGen.
This is still a WIP.

## Build

Currently only tested for macOS.
Should work for Linux, but probably not for Windows right now.

```shell
git clone https://github.com/capital-G/SC_JSFX.git

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

Afterwards symlink or copy the content of the `install` folder to `Platform.userExtensionDir`.

## Demo

Scripts are transferred to the server via Buffers - so all source code will be translated to its ascii representation.

All inputs are available via the variables `in0`, `in1`, ... and the outputs need to be written to `out0`, `out1`, ...

### In = Output * 0.5

```supercollider
// start the server
s.boot;

// create a buffer which stores the code
~code = JSFX.codeBuffer("out0 = in0 * 0.5;");

// spawn a synth which evaluates our script
(
Ndef(\x, {JSFX.ar(
	1, // numOutputs
	~code, // script to use
	SinOsc.ar(200.0)), // ... the inputs to the script
}).scope;
)
```

### Single sample feedback

#### One pole filter

```supercollider
(
~code = JSFX.codeBuffer("
y1 += 0; // make the variable persistent across runs
alpha = 0.95;
// the one pole filter formula
out0 = alpha * y1 + (1.0 - alpha) * in0;
y1 = out0; // write value to history
");
)

Ndef(\x, {JSFX.ar(1, ~code, Saw.ar(400.0)) * 0.2}).play;
```

### Feedback SinOsc

A phase modulatable `SinOscFB`

```supercollider
(
~code = JSFX.codeBuffer("
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
");
)

(
Ndef(\x, {
	var sig = JSFX.ar(1, ~code,
		\freq.ar(100.0), // in0 = freq
		\fb.ar(0.6, spec: [0.0, pi]), // in1 = fb
		SinOsc.ar(\phaseModFreq.ar(1000.0 * pi)) * \modAmt.ar(0.0, spec:[0.0, 1000.0]),  // in2 = phaseMod
	);
	sig * 0.1;
}).play.gui;
)
```

### Modulate parameters

```supercollider
~code = JSFX.codeBuffer("out0 = in0 * in1;");

Ndef(\x, {JSFX.ar(1, ~code.bufnum, SinOscFB.ar(200.0, 1.3), LFPulse.ar(5.2, width: 0.2)) * 0.2}).play;
```

### Multi-channel

```supercollider
~code = JSFX.codeBuffer("out0 = in0 * in1; out1 = in0 * in2");

(
Ndef(\y, {JSFX.ar(2, ~code.bufnum, 
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
* [ ] Handle multi-channel expansion?
* [ ] kr version?
* [ ] Turn on compiler optimization for platforms
* [ ] Expose sliders?
* [ ] Use WDL GUI?
* [ ] Have support with existing JSFX plugins?

## License

EEL2 and WDL by *Cockos* are BSD licensed.

This project is GPL-3.0 licensed.
