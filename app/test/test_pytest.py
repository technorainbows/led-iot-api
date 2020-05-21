#!/usr/bin/env python3 -u

"""Test all API routes."""
import json
import sys
sys.path.append('../../LED-IOT-api')


"""Load client secrets."""
with open('./client_secrets.json', 'r') as myfile:
    data = myfile.read()
data = json.loads(data)
# print(data)
client_secrets = data['web']
print("token loaded: ", client_secrets['auth_token'])


def test_authorization(client):
    """Make a test call to /Devices/<device>."""
    response = client.get("/Devices/device200",
                          headers={"Authorization": 'Bearer please '})

    assert response.status_code == 403


def test_get_device(client):
    """Make a test call to /Devices/<device>."""
    response = client.get("/Devices/device200",
                          headers={"Authorization": 'Bearer ' + client_secrets['auth_token']})

    assert response.status_code == 200
    assert response.json == [
        "device200",
        {
            "brightness": "255",
            "name": "Default",
            "onState": "true"
        }
    ]


def test_delete_device(client):
    """Make a test call to /Devices/<device>."""
    response = client.delete("/Devices/device200",
                             headers={"Authorization": 'Bearer ' + client_secrets['auth_token']})

    assert response.status_code == 200
    assert response.json == [
        "Device deleted",
        200
    ]


def test_put_device(client):
    """Make a test put request to /Devices/<device>."""
    response = client.put("/Devices/device50",
                          data=json.dumps({"brightness": "0"}),
                          content_type='application/json',
                          headers={"Authorization": 'Bearer ' + client_secrets['auth_token']})

    assert response.status_code == 200
    assert response.json == [
        "device50",
        {
            "brightness": "0",
            "name": "Default",
            "onState": "true"
        },
        201
    ]


def test_post_devices(client):
    """Make a test post to /Devices/."""
    response = client.post("/Devices/",
                           data=json.dumps(
                               {"brightness": "100", "name": "New Device", "onState": "False"}),
                           content_type='application/json',
                           headers={"Authorization": 'Bearer ' + client_secrets['auth_token']})

    assert response.status_code == 200
    assert response.json[1] == {
        "brightness": "100",
        "name": "New Device",
        "onState": "False"
    }


def test_check_health(client):
    """Make a test call to /"""
    response = client.get("/Health")
    assert response.status_code == 200


def test_get_devicelist(client):
    """Make a test call to /Devices/"""
    response = client.get("/Devices/",
                          headers={"Authorization": 'Bearer ' + client_secrets['auth_token']})

    assert response.status_code == 200


def test_post_hb(client):
    """Make a test post to set heartbeat."""
    response = client.post("/Devices/HB/device100",
                           data=json.dumps(
                               {"heartbeat": "device100"}),
                           content_type='application/json',
                           headers={"Authorization": 'Bearer ' + client_secrets['auth_token']})

    assert response.status_code == 200
    assert response.json is True


def test_get_hblist(client):
    """Make a test call to /Devices/HB"""
    response = client.get("/Devices/HB/",
                          headers={"Authorization": 'Bearer ' + client_secrets['auth_token']})

    assert response.status_code == 200


def test_full_hb(client):
    """Test posting and then getting a device heartbeat."""
    client.post("/Devices/HB/device100",
                data=json.dumps(
                    {"heartbeat": "device100"}),
                content_type='application/json',
                headers={"Authorization": 'Bearer ' + client_secrets['auth_token']})

    response2 = client.get("/Devices/HB/device100",
                           headers={'Authorization': 'Bearer ' + client_secrets['auth_token']})

    assert response2.status_code == 200
    assert response2.json == ["hb_device100"]
