import assert from 'node:assert/strict';
import { createRequire } from 'node:module';

const require = createRequire(import.meta.url);
const { formatMediaStatus } = require('../../package/luci-app-doorfast/htdocs/luci-static/resources/doorfast/status_model.js');

assert.deepEqual(formatMediaStatus({
    available: true,
    state: 'publishing',
    generation: 9,
    effective_capacity: 1,
    queue_drops: 2,
    rtsp_password_set: true,
    encoder_running: true,
    failure: ''
}), [
    ['State', 'Publishing'],
    ['Generation', '9'],
    ['Effective capacity', '1'],
    ['Queue drops', '2'],
    ['RTSP password', 'Set'],
    ['Encoder', 'Running'],
    ['Failure', 'None']
]);

assert.throws(function() {
    formatMediaStatus({
        available: true,
        state: 'publishing',
        generation: 9,
        effective_capacity: 1,
        queue_drops: 2,
        rtsp_password_set: 'secret',
        encoder_running: true,
        failure: ''
    });
}, /boolean/);
