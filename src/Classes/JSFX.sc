JSFX : MultiOutUGen {
	*ar {|numOutputs, scriptBuffer, inputs|
		^this.new1('audio', numOutputs, scriptBuffer, inputs.asArray.size, *inputs.asArray.postln);
	}

	init { |... theInputs|
		inputs = theInputs;
		^this.initOutputs(theInputs[0], 'audio');
	}

	*codeBuffer {|code, server=nil|
		var buffer;
		server = server ? Server.default;
		buffer = Buffer.loadCollection(server, code.ascii);
		^buffer;
	}
}
