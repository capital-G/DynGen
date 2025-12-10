DynGenDef {
	classvar <all;
	var <name;
	var <hash;
	var <>code;

	// private
	classvar counter;

	*initClass {
		all = IdentityDictionary();
		counter = 0;
	}

	*new {|name, code|
		var res;
		var hash;

		name = name.asSymbol;
		res = all[name];
		if(res.notNil, {
			if(code.isNil.not, {
				res.code = code;
			});
			^res;
		});
		hash = DynGenDef.prHashSymbol(name);
		res = super.newCopyArgs(name, hash, code ? "");
		all[name] = res;
		^res;
	}

	*load {|name, path|
		var code = File.readAllString(path.asAbsolutePath);
		^this.new(name, code);
	}

	load {|path|
		code = File.readAllString(path.asAbsolutePath);
	}

	send {|server|
		var servers = (server ?? { Server.allBootedServers }).asArray;
		servers.do { |each|
			if(each.hasBooted.not) {
				"Server % not running, could not send DynGenDef.".format(server.name).warn
			};
			this.prSendScript(each);
		}
	}

	prSendScript {|server|
		var message = [\cmd, \dyngenscript, hash, code];
		if(message.flatten.size < (65535 div: 4), {
			server.sendMsg(*message);
		}, {
			this.prSendFile(server);
		});
	}

	prSendFile {|server|
		var tmpFilePath = PathName.tmp +/+ "%_%".format(hash.asString, counter);
		counter = counter + 1;

		if (server.isLocal.not, {
			"DynGenDef % could not be added  to server % because it is too big for sending via OSC and server is not local".format(
				name,
				server,
			).warn;
			^this;
		});

		File.use(tmpFilePath, "w", {|f|
			f.write(code);
		});

		server.sendMsg(\cmd, \dyngenfile, hash, tmpFilePath);

		fork {
			var deleteSuccess;
			server.sync;
			deleteSuccess = File.delete(tmpFilePath);
			if (deleteSuccess.not, {
				"Could not delete temp file % of DynGenDef %".format(tmpFilePath, name).warn;
			});
		}
	}

	*prHashSymbol {|symbol|
		// hash numbers are too high to represent as a float32
		// on the server, so we have to scale those down.
		// 2**20 seems okayish?
		^(symbol.hash.abs % (2**20) * symbol.hash.sign).asInteger;
	}
}

// UGen code

DynGen : MetaDynGen {
	*ar {|numOutputs, script ...inputs|
		^super.ar(numOutputs, script, 0.0, *inputs);
	}
}

DynGenRT : MetaDynGen {
	*ar {|numOutputs, script ...inputs|
		^super.ar(numOutputs, script, 1.0, *inputs);
	}
}

MetaDynGen : MultiOutUGen {
	*ar {|numOutputs, script, realTime ...inputs|
		script = case
		{script.isKindOf(DynGenDef)} {script.hash}
		{script.isKindOf(String)} {DynGenDef.prHashSymbol(script.asSymbol)}
		{script.isKindOf(Symbol)} {DynGenDef.prHashSymbol(script)}
		{Error("Script input needs to be a DynGenDef object or a symbol, found %".format(script.class)).throw}

		^this.multiNew('audio', numOutputs, script.asFloat, realTime, *inputs);
	}

	init { |numOutputs ... theInputs|
		inputs = theInputs;
		inputs = inputs.asArray.collect({|input|
			if(input.rate != \audio, {
				K2A.ar(input);
			}, {
				input;
			});
		});
		^this.initOutputs(numOutputs, 'audio');
	}
}
