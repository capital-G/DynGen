JSFX : MultiOutUGen {
	*ar {|numOutputs, scriptBuffer ...inputs|
	    scriptBuffer = if(scriptBuffer.class == Buffer, {scriptBuffer.bufnum}, {scriptBuffer});
		^this.multiNew('audio', numOutputs, scriptBuffer, inputs.size, *inputs);
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
