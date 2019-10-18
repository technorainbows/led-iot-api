#!/usr/bin/env python3
"""API for LED IOT webapp."""
from flask_cors import CORS
from flask import Flask, jsonify, request, json
from flask_restplus import Api, Resource, fields, cors
# from werkzeug.contrib.fixers import ProxyFix
import redis
from redis.exceptions import WatchError
import json
import time
import logging

logging.basicConfig(
    format='%(asctime)s %(levelname)-8s %(message)s',
    level=logging.WARNING,
    datefmt='%Y-%m-%d %H:%M:%S')


app = Flask(__name__)
# app.wsgi_app = ProxyFix(app.wsgi_app)
api = Api(app, version='0.3', title='LED API',
          description='A simple LED IOT API', doc='/docs'
          )

CORS(app)
# ns = api.namespace('Devices', description='DEVICES operations')

default = {
            'onState': 'true',
            'brightness': '255',
            'name': 'Default'
        }


"""Single Device Data Model"""
device = api.model('Device', {
    'onState': fields.String(description='The on/off state',
                   attribute='onState', required=False, default=True),
    'brightness': fields.String(description='The LED brightness',
                   attribute='brightness', min=0, max=255, required=False, default=255),
    'name': fields.String(description="name of the device",
                   attribute='name', required=False, default='N/A')
})

"""Device List Data Model"""
list_of_devices = api.model('ListedDevices', {
    'id': fields.String(required=True, description='The device ID'),
    'device': fields.Nested(device, description='The device'),
})

heartbeat = api.model('Heartbeat', {
    'heartbeat': fields.String(required=False, description='Device heartbeat')
    })


# def abort_if_device_not_found(device_id):
#     if device_id not in DEVICES:
#         api.abort(404, "Device {} doesn't exist".format(device_id))


class Redis(object):
    def __init__(self):
        """Initialize Redis Object."""
        self.redis = redis.Redis(host='localhost', port=6379, db=0)

    # def bytesToString(self, byteDict):
    #     """Receives a dictionary of bytes and returns a dictionary of strings."""
    #     # Initialising empty dictionary
    #     strDict = {}
    #     print ("byteDict type: ", type(byteDict))
    #     # Convert dictionary items from bytes to strings
    #     index = 0
    #     for key, value in byteDict.items():
    #       y[key.decode("utf-8")] = value.decode("utf-8")
    #     for item in byteDict:
    #         # if(index%2)
    #         newItem = item.decode("utf-8")
    #         if(type(newItem) == str):
    #             strDict[index] = item.decode("utf-8")
    #         index += 1
    #     print("new string dict: ", strDict)
    #     return strDict

    def get(self, key):
        """Get device from redit if it exists, otherwise load with default."""
        with self.redis.pipeline() as pipe:
            while True:
                try:
                    pipe.watch(key)
                    if(pipe.exists(key) == 0):
                        logging.info("get: no device")
                        pipe.multi()
                        pipe.hmset(key, default)
                        # time.sleep(5)
                        pipe.execute()
                        device = default
                        break
                    else:
                        device = pipe.hgetall(key)
                        logging.info("get: device found {}".format(device))
                        # Initialising empty dictionary
                        newDict = {}

                        # Convert dictionary items from bytes to strings
                        for dkey, value in device.items():
                            newDict[dkey.decode("utf-8")] = value.decode("utf-8")
                        device = newDict
                        logging.info("get: device found: {}".format(device))
                        break
                except WatchError:
                    logging.warning("get: watcherror, trying again")
                    continue

        return device

    def set(self, key, field, value):
        """Set value in given device if exists, else initialize device to default first."""
        with self.redis.pipeline() as pipe:
            while True:
                try:
                    pipe.watch(key)
                    if(pipe.exists(key) == 0):
                        logging.info("set: no device")
                        pipe.multi()
                        pipe.hmset(key, default)
                        pipe.hmset(key, {field: value})
                        # time.sleep(5)
                        device = pipe.hgetall(key)
                        pipe.execute()
                        break

                    else:
                        pipe.hmset(key, {field: value})

                        # keyTest = keyTest.decode("utf-8")
                        # print("key found: ", device)

                        device = pipe.hgetall(key)
                        logging.info("set: device found: {}".format(device))
                        break

                except WatchError:
                    logging.warning("set: watcherror, trying again")
                    continue

            # Initialising empty dictionary
            newDict = {}

            # Convert dictionary items from bytes to strings
            for dkey, value in device.items():
                newDict[dkey.decode("utf-8")] = value.decode("utf-8")
            device = newDict
            logging.info("set: new device = {}".format(device))
            return (device)

    def delete(self, key):
        """Delete a key from Redis client."""
        response = self.redis.delete(key)
        logging.info("response: {}".format(response))
        return response

    def keys(self, param):
        """Return list of all keys matching parameter."""
        keys = []
        for key in self.redis.scan_iter(match=param+"*"):
            # print("key: ", key)
            keys.append(key.decode())
        # print("keys returned: ", keys)
        return keys

    def setHB(self, heartbeat, time):
        """Set heartbeat key in redis that expires in provided time."""
        response = self.redis.setex(heartbeat, time, 5)
        return response


"""Initialize REDIS object"""
REDIS = Redis()


""""""""""""""""""""""""""""""""""""
"""Heartbeat Methods"""
""""""""""""""""""""""""""""""""""""
@api.route('/Devices/HB/<string:device_id>', methods=['GET', 'POST'])
class Heartbeat(Resource):
    """Update and check on a given device's heartbeat/online status."""

    @api.response(200, 'Success')
    def get(self, device_id):
        """Check if a given heartbeat exists."""
        logging.info("checking device HB")
        heartbeat = "hb_" + device_id
        response = REDIS.keys(heartbeat)
        logging.info("HB get response = {}".format(response))
        if response == []:
            logging.error('Error, device not found')
            return ('Error, device not found', 404)
        else:
            return jsonify(heartbeat, response, 200)

    @api.response(200, 'Success')
    # @api.param('heartbeat')
    @api.expect(heartbeat, validate=False)
    def post(self, device_id):
        """Set a heartbeat."""
        hbTime = 5
        heartbeat = "hb_" + device_id
        response = REDIS.setHB(heartbeat, hbTime*2)
        logging.info("HB post response = ".format(response))
        payload = api.payload
        # print("payload = ", api.payload)
        if response:
            return jsonify(heartbeat, response, 200)
        else:
            return "Error, failled to set heartbeat.", 404

@api.route('/Devices/HB/', methods=['GET'])
class Heartbeats(Resource):
    """Monitor which devices are online or not via heartbeat."""

    @api.response(202, 'Success')
    def get(self):
        """Return a list of all heartbeats."""
        keys = REDIS.keys("hb")
        logging.info("HB getting all keys: {}".format(keys))
        return jsonify(keys)


""""""""""""""""""""""""""""""""""""
"""Single Device Response Methods"""
""""""""""""""""""""""""""""""""""""
"""TODO: add list of device_ids from redis to check if decive there"""
@api.route('/Devices/<string:device_id>', methods=['GET', 'POST', 'PUT', 'DELETE'])
@api.doc(responses={404: 'Device not found', 200: 'Device found'},
         params={'device_id': 'The Device ID'})
@api.doc(description='device_id should be {0}'.format(', '.join(REDIS.keys("device_id"))))
class Device(Resource):
    """Show a single device's properties and lets you delete them or change them."""

    @api.response(203, 'Success', device)
    def get(self, device_id):
        """Fetch a given resourc."""
        # abort_if_device_not_found(device_id)
        redisGet = REDIS.get(device_id)
        # print("device_id: ", device_id)
        # print('redisGet: ', redisGet)
        return jsonify(device_id, redisGet, 203)
        # return jsonify(DEVICES[device_id],200)

    @api.doc(responses={204: 'Device deleted'})
    def delete(self, device_id):
        """Delete a given resource."""
        # abort_if_device_not_found(device_id)
        response = REDIS.delete(device_id)
        if response:
            logging.info("Device deleted: {}".format(device_id))
            return 'Device deleted', 204
        else:
            logging.error('Error, device not found: {}'.format(device_id))
            return 'Error: device not found', 404

    @api.expect(device, validate=True)
    # @api.doc(parser=parser)
    @api.response(200, 'Success', device)
    def put(self, device_id):
        """Update a given resource's field with new property value received."""
        for field in device:
            # print("field: ", field)
            if field in request.json:
                print("field found: ", field, "value = ",request.json.get(field) )
                # let value = request.json.get(field)
                REDIS.set(device_id, field, request.json.get(field))

        # print('returned redisSet = ', redisSet)
        return jsonify(device_id, REDIS.get(device_id), 200)
        # return jsonify(DEVICES[device_id])


""""""""""""""""""""""""""""""""""""''
"""List of Devices Response Methods"""
""""""""""""""""""""""""""""""""""""''
@api.route('/Devices/')
class DeviceList(Resource):
    """Shows a list of all devices, and lets you POST to add new tasks."""

    @api.response(200, 'Success', list_of_devices)
    def get(self):
        """Return a list of all devices and their properties."""
        DEVICES = REDIS.keys("device_id")
        # print("returning Device List: ", DEVICES)
        return jsonify(DEVICES,200)
        # return ([{'id': id, 'device': ListedDevices} for id, ListedDevices in DEVICES.items()], 200)

    @api.expect(device, validate=True)
    def post(self):
        """Create a new device with next id."""
        device_id = 'device%d' % (len(DEVICES) + 1)
        # DEVICES[device_id] = device
        # print("json request received: ", request.json)

        """Update a given resource's field with new property value received."""
        for field in device:
            # print("field: ", field)
            if field in request.json:
                # print("field found: ", field, "value = ",request.json.get(field) )
                REDIS.set(device_id, field, request.json.get(field))

        return jsonify(device_id, REDIS.get(device_id), 201)
        # return jsonify(redisSet)
        # return jsonify(DEVICES[device_id], 201)


@app.errorhandler(404)
def not_found(error):
    logging.error('Error, device not found')
    return (jsonify({'error': 'Not found'}), 404)


if __name__ == '__main__':
    app.run(host='0.0.0.0', port=5000, debug=True)
