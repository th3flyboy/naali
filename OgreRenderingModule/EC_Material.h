// For conditions of distribution and use, see copyright notice in license.txt

#ifndef incl_OgreRenderer_EC_Material_h
#define incl_OgreRenderer_EC_Material_h

#include "StableHeaders.h"
#include "IComponent.h"
#include "Core.h"
#include "OgreModuleApi.h"

namespace OgreRenderer { class OgreRenderingModule; };

/// Ogre material modifier component
/**
<table class="header">
<tr>
<td>
<h2>Material</h2>
Modifies an existing material asset by a parameter list and outputs it as another asset.
This allows network-replicated changes to material assets.
Registered by OgreRenderer::OgreRenderingModule.

<b>Attributes</b>:
<ul>
<li>QVariantList parameters
<div>The parameters to apply.
<li>QString inputMat
<div>Input material asset reference. Can also be a submesh number in the same entity's EC_Mesh, or empty to use submesh 0 material.
<li>QString outputMat
<div>Output material asset.
</ul>

<b>Exposes the following scriptable functions:</b>
<ul>
<li>...
</ul>

<b>Reacts on the following actions:</b>
<ul>
<li>...
</ul>
</td>
</tr>

Does not emit any actions.

</table>
*/
class OGRE_MODULE_API EC_Material : public IComponent
{
    Q_OBJECT
    
public:
    /// Do not directly allocate new components using operator new, but use the factory-based SceneAPI::CreateComponent functions instead.
    explicit EC_Material(Framework *fw);

    virtual ~EC_Material();

    Q_PROPERTY(QVariantList parameters READ getparameters WRITE setparameters);
    DEFINE_QPROPERTY_ATTRIBUTE(QVariantList, parameters);

    Q_PROPERTY(QString inputMat READ getinputMat WRITE setinputMat);
    DEFINE_QPROPERTY_ATTRIBUTE(QString, inputMat);

    Q_PROPERTY(QString inputMat READ getoutputMat WRITE setoutputMat);
    DEFINE_QPROPERTY_ATTRIBUTE(QString, outputMat);

    COMPONENT_NAME("EC_Material", 31)
public slots:

private slots:
    void OnAttributeUpdated(IAttribute* attribute);

private:
    void ProcessMaterial();
};

#endif