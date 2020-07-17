import lcm
import threading, queue

from PythonQt.QtGui import *
from PythonQt.QtCore import *

import dairlib.lcmt_robot_output
from director import visualization as vis
from director import lcmUtils
import director.applogic
import director.mainwindowapp
from director.debugVis import DebugData
import director.objectmodel as om
from pydairlib.common import FindResourceOrThrow

from pydrake.math import RigidTransform, RotationMatrix
import pydrake.systems.framework
import pydrake.multibody.plant
import pydrake.multibody.parsing
import numpy as np
import json
import sys
from collections import deque
import re
from pydrake.common.eigen_geometry import Quaternion

class VisualizationGui(QWidget):

    def __init__(self, parent = None):
        super(VisualizationGui, self).__init__(parent)

        # GUI attributes
        self.checkBoxes = {}
        self.checkBoxesPrevState = {}
        self.resetBtn = None
        self.clearBtn = None
        self.checkBoxArea = None
        self.reset = False
        self.delete = False
        self.clear = False
        self.ready = False
        self.lcmObjects = {}
        self.subscriptions = []

        # JSON attributes
        self.channel = ""
        self.data = None
        self.modelFile = None
        self.weldBody = None
        self.shapes = {}
        self.plant = None

        # create the GUI
        self.setWindowTitle("Visualization GUI")
        self.vbox = QVBoxLayout()
        hbox = QHBoxLayout()

        # create the JSON directory reader
        hbox.addWidget(QLabel("Enter JSON file directory"))
        self.JSONInput = QLineEdit("./director/scripts/test/testJSON.json")
        hbox.addWidget(self.JSONInput)
        self.readJSON = QPushButton('Read JSON')
        self.readJSON.clicked.connect(self.readJSONFile)
        hbox.addWidget(self.readJSON)
        self.vbox.addLayout(hbox)

    def readJSONFile(self):
        '''
        Function for reading the JSON input file and populating the JSON
        and GUI attributes
        '''
        # load only if input is not empty
        if (self.JSONInput.text != ""):
            self.json_file = self.JSONInput.text
            with open(self.json_file) as json_file:
                self.data = json.load(json_file)

            self.ready = True
            self.modelFile = self.data['model_file']

            if ('weld-body' in self.data):
                self.weldBody = self.data['weld-body']

            # create each object/shape to be drawn
            for data in self.data['data']:
                newObject = ObjectToDraw(data)

                # if this is a shape of type lcm then create an additional separate object
                if (newObject.category == "lcm" and (newObject.name not in self.lcmObjects)):
                    if (newObject.type == "axis"):
                        self.lcmObjects[data['name']] = LCMMessage(newObject.source_data['abstract_channel'], newObject.source_data['abstract_type'],
                        newObject.source_data["abstract_field"], newObject.source_data["quaternion_index"], 0, 0)
                    else:
                        self.lcmObjects[data['name']] = LCMMessage(newObject.source_data['abstract_channel'], newObject.source_data['abstract_type'],
                        newObject.source_data["abstract_field"], newObject.source_data["x_index"], newObject.source_data["y_index"], newObject.source_data["z_index"])

                # if there exists a shape with the given name then simply update it
                if (newObject.name not in self.shapes):
                    self.shapes[newObject.name] = newObject
                else:
                    self.shapes[newObject.name].update(newObject)

            # fill the checkboxes for each data with its name and add the "reset" and "clear" buttons
            self.placeCheckBoxes()
            if (self.resetBtn == None):
                self.resetBtn = QPushButton('Reset')
                self.resetBtn.clicked.connect(self.deleteShapes)
                self.vbox.addWidget(self.resetBtn)

            if (self.clearBtn == None):
                self.clearBtn = QPushButton('Clear History')
                self.clearBtn.clicked.connect(self.clearHistory)
                self.vbox.addWidget(self.clearBtn)

            if (self.plant == None):
                # Create the plant
                builder = pydrake.systems.framework.DiagramBuilder()
                self.plant, scene_graph = \
                    pydrake.multibody.plant.AddMultibodyPlantSceneGraph(builder, 0)
                pydrake.multibody.parsing.Parser(self.plant).AddModelFromFile(
                FindResourceOrThrow(self.modelFile))

                # determine if there is a need to use the weld a body part
                if (self.weldBody != None):
                    self.plant.WeldFrames(self.plant.world_frame(),
                        self.plant.GetFrameByName(self.weldBody), RigidTransform.Identity())
                self.plant.Finalize()
                self.context = self.plant.CreateDefaultContext()

            # start listenning to the main state LCM messages
            lcmUtils.addSubscriber(self.data['channelName'], messageClass=eval(self.data['channel_type']), callback=self.state_handler)

            # add more LCM subscriptions depending on the number of "lcm" data
            for name in self.lcmObjects:
                lcmMessage = self.lcmObjects[name]
                subscriber = None
                if (self.shapes[name].type == "axis"):
                    subscriber = lcmUtils.addSubscriber(lcmMessage.channel, messageClass=eval(lcmMessage.type), callback=lambda msg, field=lcmMessage.field, name=name, x=lcmMessage.x, y=0, z=0: self.abstract_handler(msg, field, name, x, y, z))
                else:
                    subscriber = lcmUtils.addSubscriber(lcmMessage.channel, messageClass=eval(lcmMessage.type), callback=lambda msg, field=lcmMessage.field, name=name, x=lcmMessage.x, y=lcmMessage.y, z=lcmMessage.z: self.abstract_handler(msg, field, name, x, y, z))

                self.subscriptions.append(subscriber)

    def deleteShapes(self):
        '''
        Function for setting the flag for deleting all shapes currently present
        '''
        if (self.delete == False):
            self.delete = True

    def clearHistory(self):
        '''
        Function for setting the flag for clearing the history of any line present
        '''
        if (self.clear == False):
            self.clear = True

    def placeCheckBoxes(self):
        '''
        Function for placing the checkboxes of the GUI. Each checkbox corresponds
        to a shape/object that has been drawn with the corresponding color and
        extension
        '''
        if (self.ready == True):
            addToGUI = False
            if (self.checkBoxArea == None):
                self.checkBoxArea = QVBoxLayout()
                addToGUI = True
            for name in self.shapes:
                # create appropriate symbol extention
                extension = " •"
                type = self.shapes[name].type
                if (type == "point"):
                    extension = " •"
                elif (type == "line"):
                    extension = " ---"
                elif (type == "axis"):
                    extension = " |_"

                # create each checkbox and conditionally add it to the GUI
                addToList = False
                if (name not in self.checkBoxes):
                    self.checkBoxes[name] = QCheckBox(name + extension)

                    if (self.shapes[name].type == "point" or self.shapes[name].type == "line"):
                        color = self.shapes[name].color
                        self.checkBoxes[name].setStyleSheet("color: rgb("+str(color[0] * 255)+", "+str(color[1] * 255)+", "+str(color[2] * 255)+")")

                    addToList = True
                self.checkBoxes[name].setChecked(True)
                self.checkBoxesPrevState[name] = True
                if (addToList == True):
                    self.checkBoxArea.addWidget(self.checkBoxes[name])
                if (addToGUI == True):
                    self.vbox.addLayout(self.checkBoxArea)

    def checkBoxChecked(self, name):
        '''
        Function for showing a shape when its corresponding checkbox is checked

        name: name of corresponding shape
        '''
        if (self.shapes[name].object != None and self.shapes[name].object.getProperty('Visible') == False):
            self.shapes[name].object.setProperty('Visible', True)

    def checkBoxNotChecked(self, name):
        '''
        Function for hiding a shape when its corresponding checkbox is unchecked

        name: name of corresponding shape
        '''
        if (self.shapes[name].object != None and self.shapes[name].object.getProperty('Visible') == True):
            self.shapes[name].object.setProperty('Visible', False)

    def distance(self, pt1, pt2):
        '''
        Function for computing distance between 2 given 3D points

        pt1, pt2: the 2 points whose distance will be computed
        '''
        sum = 0
        for i in range(len(pt1)):
            sum += pow(pt2[i] - pt1[i], 2)

        return math.sqrt(sum)


    def abstract_handler(self, msg, field, name, x, y, z):
        '''
        Function for handling LCM messages originating from a different
        channel than the main one

        msg: the returning LCM message
        field: the field name/path
        name: name of corresponding shape
        x, y, z: indices for each of the 3 dimensions of the location where
                 the shape is to be drawn. When the shape is an axis then x
                 will be the first index of the quaternion array and the other
                 3 will be the next indices in sequence
        '''
        self.handle_checkBox(name)
        attribute = msg

        # parse field to get the appropriate information. This is done by
        # recursively searching for the right attribute in the given message
        field = field.split(".")
        regExpr = re.compile('.+\[\d\]')
        for part in field:
            index = None
            attName = None
            if regExpr.match(part) is not None:
                index = part[len(part) - 2]
                attName = part[0:len(part) - 3:1]
                attribute = getattr(attribute, attName)[int(index)]
            else:
                index = None
                attName = part
                attribute = getattr(attribute, attName)

        currShape = self.shapes[name]

        # special case of an axis data
        if (currShape.type == "axis"):
            # get quaternion array, normalize it, and get the corresponding rotation matrix
            quaternion = []
            for i in range(x, x+4):
                quaternion.append(attribute[i])
            norm = np.linalg.norm(quaternion)
            quaternion = [x / norm for x in quaternion]
            rot_matrix = Quaternion(quaternion).rotation()

            pt_world = self.plant.CalcPointsPositions(self.context,
                self.plant.GetFrameByName(currShape.frame),
                currShape.point, self.plant.world_frame())
            next_loc = pt_world.transpose()[0]

            self.drawShape(self.shapes[name], next_loc, msg, rotation_matrix = rot_matrix)

        else:
            next_loc = [attribute[x], attribute[y], attribute[z]]
            self.drawShape(self.shapes[name], next_loc, msg)

    def handle_checkBox(self, name):
        '''
        Function for checking if a checkBox is checked or not

        name: name of corresponding shape
        '''
        if (self.checkBoxes[name].isChecked() == True):
            self.checkBoxChecked(name)

        else:
            self.checkBoxNotChecked(name)

    def state_handler(self, msg):
        '''
        Function for handling main lcm channel

        msg: the returning LCM message
        '''
        # start listenning to messages once the JSON file has been read
        if (self.ready == True):
            # TODO (for Michael): bind our more robust decoding mechanisms to python
            self.msg = msg
            self.plant.SetPositions(self.context, self.msg.position)
            self.plant.SetVelocities(self.context, self.msg.velocity)

            # iterate through each shape to draw it
            for name in self.shapes:
                self.handle_checkBox(name)
                currShape = self.shapes[name]

                # define next_loc according to each shape/object to be drawn
                next_loc = None
                if (currShape.category == "kinematic"):
                    # Use Drake's CalcPointsPositions to determine where that point is
                    # in the world
                    pt_world = self.plant.CalcPointsPositions(self.context,
                        self.plant.GetFrameByName(currShape.frame),
                        currShape.point, self.plant.world_frame())
                    next_loc = pt_world.transpose()[0]

                elif (currShape.category == "com"):
                    next_loc = self.plant.CalcCenterOfMassPosition(context = self.context)

                elif (currShape.category == "lcm"):
                    # in the case of an lcm message do not do anything as this is
                    # handled by the abstract handler
                    continue

                self.drawShape(currShape, next_loc, self.msg)

            # handle flags for clearing line histories
            if (self.clear == True):
                for shape in self.shapes.values():
                    if (shape.type == "line"):
                        om.removeFromObjectModel(shape.object)
                        shape.object = None
                        shape.points = deque()
                self.clear = False

            # handle flags for deleting objects
            if (self.delete == True):
                for shape in self.shapes.values():
                    om.removeFromObjectModel(shape.object)

                self.delete = False
                self.reset = True

            # handle flags for reseting the GUI
            if (self.reset == True):
                # delete checkboxes from GUI
                for i in reversed(range(self.checkBoxArea.count())):
                    self.checkBoxArea.itemAt(i).widget().deleteLater()

                self.resetBtn.deleteLater()
                self.clearBtn.deleteLater()

                for subscription in self.subscriptions:
                    lcmUtils.removeSubscriber(subscription)

                # reset GUI variables
                self.checkBoxes = {}
                self.checkBoxesPrevState = {}
                self.resetBtn = None
                self.clearBtn = None
                self.checkBoxArea = None
                self.reset = False
                self.delete = False
                self.clear = False
                self.ready = False
                self.lcmObjects = {}
                self.subscriptions = []

                # reset JSON attributes
                self.data = None
                self.modelFile = None
                self.weldBody = None
                self.shapes = {}
                self.plant = None

    def drawShape(self, currShape, next_loc, msg, rotation_matrix = None):
        '''
        Function for drawing shapes. Currently this supports lines, points, and
        3D axes

        currShape: the current shape to be drawn
        next_loc: the location where to draw the shape
        msg: the message from the LCM hendler that called this function
        rotation_matrix: the rotation matrix to be used for the axis shape.
                         Mainly used by the abstract handler
        '''
        # draw a continuous line
        if (currShape.type == "line"):
            # check if the duration has been initialized
            if (currShape.duration == None or len(currShape.points) == 0):
                currShape.duration = msg.utime / 1000000

            # visualize and trace line for 'history' seconds, adding points at a distance at least 10e-5
            if (((msg.utime / 1000000) - currShape.duration <= currShape.history) or currShape.history <= 0):
                # make sure to add at least 2 points before starting to check for the distance between points
                if (len(currShape.points) < 2):
                    currShape.points.append(next_loc)
                    d = DebugData()
                    d.addPolyLine(currShape.points, radius=currShape.thickness, color=currShape.color)

                    if (currShape.object == None):
                        currShape.object = vis.showPolyData(d.getPolyData(), "line")
                        currShape.object.setProperty('Color', currShape.color)
                    else:
                        currShape.object.setPolyData(d.getPolyData())

                else:
                    if (self.distance(currShape.points[-1], next_loc) >= 10e-5):
                        currShape.points.append(next_loc)
                        d = DebugData()
                        d.addPolyLine(currShape.points, radius=currShape.thickness, color=currShape.color)

                        if (currShape.object == None):
                            currShape.object = vis.showPolyData(d.getPolyData(), "line")
                        else:
                            currShape.object.setPolyData(d.getPolyData())

            elif (currShape.history > 0):
                if (len(currShape.points) == 0):
                    currShape.duration = msg.utime / 1000000
                else:
                    # visualize and trace line for 'history' seconds, adding points at a distance at least 10e-5
                    if (self.distance(currShape.points[-1], next_loc) >= 10e-5):
                        currShape.points.popleft()
                        currShape.points.append(next_loc)
                        d = DebugData()
                        d.addPolyLine(currShape.points, radius=currShape.thickness, color=currShape.color)
                        if (currShape.object == None):
                            currShape.object = vis.showPolyData(d.getPolyData(), "line")
                        else:
                            currShape.object.setPolyData(d.getPolyData())

        # draw a point
        elif (currShape.type == "point"):
            d = DebugData()
            d.addSphere(next_loc, radius = currShape.radius)
            # create a new point
            if (currShape.created == True):
                currShape.object = vis.showPolyData(d.getPolyData(), "sphere")
                # set color and transparency of point
                currShape.object.setProperty('Color', currShape.color)
                currShape.object.setProperty('Alpha', currShape.alpha)
                currShape.created = False
            else:
                # update the location of the last point
                currShape.object.setPolyData(d.getPolyData())

        # draw a set of axes
        elif (currShape.type == "axis"):
            # get the rotation matrix
            rot_matrix = None
            if (currShape.category != "lcm"):
                rigTrans = self.plant.EvalBodyPoseInWorld(self.context, self.plant.GetBodyByName(currShape.frame))
                rot_matrix = rigTrans.rotation().matrix().transpose()
            else:
                rot_matrix = rotation_matrix

            colors = [[1, 0, 0], [0, 1, 0], [0, 0, 1]]

            d = DebugData()
            for i in range(3):
                d.addArrow(next_loc, next_loc + (rot_matrix[i]/4), headRadius=0.03, color = colors[i])

            # create the 3 axes
            if (currShape.created == True):
                currShape.object = vis.showPolyData(d.getPolyData(), "axis", colorByName='RGB255')
                currShape.object.setProperty('Alpha', currShape.alpha)
                currShape.created = False
            else:
                # update the location of the last point
                currShape.object.setPolyData(d.getPolyData())

class ObjectToDraw():
    '''
    Wrapper class for any object/shape being drawn
    '''
    def __init__(self, data):
        # set attributes from given data (originating from input JSON file)
        self.name = data['name']

        info = data['info']
        self.source_data = info['source_data']
        type_data = info['type_data']

        self.category = self.source_data['category']

        if (self.category == "kinematic"):
            self.frame = self.source_data['frame']
            self.point = self.source_data['point']

        self.alpha = type_data['alpha']
        self.type = type_data['type']
        self.created = True
        self.object = None

        if (self.type == "line"):
            self.color = type_data['color']
            self.thickness = type_data['thickness']
            self.history = type_data['history']
            self.duration = None
            self.points = deque()

        elif (self.type == "point"):
            self.color = type_data['color']
            self.radius = type_data['radius']

        elif (self.type == "axis"):
            if (self.category == "lcm"):
                self.frame = self.source_data['frame']
                self.point = self.source_data['point']
                self.quaternion_index = self.source_data['quaternion_index']
            self.thickness = type_data['thickness']

    def update(self, otherObject):
        '''
        Function for updating certain attributes of already existing object

        otherObject: the other ObcectToDraw instance
        '''
        self.alpha = otherObject.alpha

        if (self.category == "kinematic"):
            self.frame = otherObject.frame
            self.point = otherObject.point

        if (self.type == "line"):
            self.color = otherObject.color
            self.thickness = otherObject.thickness
            self.history = otherObject.history

        elif (self.type == "point"):
            self.color = otherObject.color
            self.radius = otherObject.radius

        elif (self.type == "axis"):
            self.thickness = otherObject.thickness


class LCMMessage():
    '''
    Wrapper class for LCM messages
    '''
    def __init__(self, channel, type, field, x_index, y_index, z_index):
        self.channel = channel
        self.type = type
        self.field = field
        self.x = x_index
        self.y = y_index
        self.z = z_index

# Adding the widget
panel = VisualizationGui()
panel.show()
panel.setLayout(panel.vbox)
app.addWidgetToDock(panel, QtCore.Qt.RightDockWidgetArea)
