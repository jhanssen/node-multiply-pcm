/*global require,module,process*/

"use strict";

const bindings = require("bindings")("multiply-pcm.node");

const { Transform } = require("stream");

class Multiply extends Transform {
    constructor(options) {
        super(options);

        if (typeof options.gain !== "number") {
            throw "Need a gain number";
        }

        this._lastFormat = {};
        this._pending = [];

        this._multiply = bindings.New(() => {
            this.push(this._pending[0].chunk);
            this._pending[0].done();

            this._pending.splice(0, 1);

            if (!this._pending.length) {
                return;
            }
            process.nextTick(() => {
                bindings.Feed(this._multiply, this._pending[0].chunk);
            });
        }, options.gain);

        this._format(options);

        this._format = this._format.bind(this);
        this.on("pipe", this._pipe);
        this.on("unpipe", this._unpipe);
    }

    setGain(gain) {
        bindings.SetGain(this._multiply, gain);
    }

    _transform(chunk, encoding, done) {
        // console.log("want to transform", chunk.length, typeof done);
        this._pending.push({ chunk: chunk, done: done });
        if (this._pending.length === 1) {
            bindings.Feed(this._multiply, chunk);
        }
    }

    _pipe(source) {
        this._format(source);
        source.once("format", this._format);
    }

    _unpipe(source) {
        source.removeListener("format", this._format);
    }

    _format (opts) {
        let set = false;
        if (typeof opts.channels === "number") {
            set = true;
            this._lastFormat.channels = opts.channels;
        }
        if (typeof opts.bitDepth === "number") {
            set = true;
            this._lastFormat.bitDepth = opts.bitDepth;
        }
        if (typeof opts.sampleRate === "number") {
            set = true;
            this._lastFormat.sampleRate = opts.sampleRate;
        }
        if (typeof opts.signed === "boolean") {
            set = true;
            this._lastFormat.signed = opts.signed;
        } else {
            // signed defaults to true for 16, 24 and 32
            this._lastFormat.signed = this._lastFormat.bitDepth !== 8;
        }
        if (set) {
            bindings.SetFormat(this._multiply,
                               this._lastFormat.channels, this._lastFormat.bitDepth,
                               this._lastFormat.sampleRate, this._lastFormat.signed);
            this.emit("format", this._lastFormat);
        }
    }
};

module.exports = { Multiply: Multiply };
