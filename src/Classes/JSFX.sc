JSFX : UGen {
	*ar {|numOutputs, inputs, script|
	    // @todo add script
		^this.multiNew('audio', numOutputs, inputs.asArray.size, inputs);
	}
}
