JSFX : MetaJSFX {
	*ar {|numOutputs, scriptBuffer ...inputs|
		^super.ar(numOutputs, scriptBuffer, 0.0, *inputs);
	}

	init { |... theInputs|
		inputs = theInputs;
		^this.initOutputs(theInputs[0], 'audio');
	}
}

JSFXRT : MetaJSFX {
	*ar {|numOutputs, scriptBuffer ...inputs|
		^super.ar(numOutputs, scriptBuffer, 1.0, *inputs);
	}

	init { |... theInputs|
		inputs = theInputs;
		^this.initOutputs(theInputs[0], 'audio');
	}
}

MetaJSFX : MultiOutUGen {
	*ar {|numOutputs, scriptBuffer, realTime ...inputs|
	    scriptBuffer = if(scriptBuffer.class == Buffer, {scriptBuffer.bufnum}, {scriptBuffer});
		^this.multiNew('audio', numOutputs, scriptBuffer, inputs.size, realTime, *inputs);
	}

	init { |... theInputs|
		^this.initOutputs(theInputs[0], 'audio');
	}

	*codeBuffer {|code, server=nil|
		var buffer;
		server = server ? Server.default;
		buffer = Buffer.loadCollection(server, code.ascii);
		^buffer;
	}
}
