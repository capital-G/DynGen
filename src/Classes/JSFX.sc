JSFX : UGen {
	*ar {|numOutputs, scriptBuffer, inputs|
		^this.multiNew('audio', numOutputs, scriptBuffer, inputs.asArray.size, inputs);
	}

	*codeBuffer {|code, server=nil|
	    var buffer;
	    server = server ? Server.default;
	    buffer = Buffer.loadCollection(server, code.ascii);
	    ^buffer;
	}
}
