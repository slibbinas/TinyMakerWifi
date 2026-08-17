  /* ---- Tiltelis sliceriui (0.17 SL-mod) --------------------------------------
     Visa scena, kamera, pjuvis ir three.js krovimas jau yra virsuje - sliceriui
     truksta tik keliu duru i ja. Modelio tinklelis ateina ne is SD sluoksniu, o
     tiesiai is naršyklėje supjaustyto STL, tad jam reikia savo iejimo.
     Perkelta is experimental2, kur ta grandine buvo isbandyta ant gelezies. */
  let supMesh=null;
  /* Mygtukai, kurie turi prasme TIK supjaustytiems duomenims. Sliceryje jie
     slepiami (nera ko pjauti, nera voksleliu), o gl3dShow juos grazina. */
  const SLICER_HIDE=['gl3dCage','gl3dDet','gl3dClip','gl3dRaw'];

  // Modelio tinklelis is slicerio (Float32Array pozicijos). home===false: tas pats
  // modelis, tik pakeistas - kameros neliesti.
  window.gl3dMesh=(positions,home)=>{
    try{
      if(!ren)init();
      const cv=$('printPreviewCanvas');
      host.style.display='block';          // PRIES matavima, kitaip clientWidth=0
      const w=host.clientWidth||cv.clientWidth||720,h=host.clientHeight||cv.clientHeight||420;
      ren.setSize(w,h);cam.aspect=w/h;cam.updateProjectionMatrix();
      if(mesh){scene.remove(mesh);mesh.geometry.dispose();mesh.material.dispose();}
      const geo=new T.BufferGeometry();
      geo.setAttribute('position',new T.BufferAttribute(positions,3));
      geo.computeVertexNormals();
      mesh=new T.Mesh(geo,new T.MeshStandardMaterial({color:0xe8720c,roughness:.55,
        metalness:.12,side:T.DoubleSide}));
      scene.add(mesh);
      /* Kesas privalo nusimusti: kitas iprastos perziuros piesimas kitaip parodytu
         SLICERIO tinkleli kaip jau paruosta modeli (tas pats raštas kaip build()). */
      key3d='';framedFor='';
      geo.computeBoundingBox();
      const bb=geo.boundingBox,ctr=new T.Vector3(),sz=new T.Vector3();
      bb.getCenter(ctr);bb.getSize(sz);
      if(home!==false&&typeof setHome==='function')setHome(ctr,Math.max(sz.x,sz.y,sz.z));
      /* Valdikliu grupes. Perkeldamas si gabala pirma karta juos PRALEIDAU, tad
         slicerio rezime likdavo tik apatine irankiu juosta, o sukimo, stumimo ir
         priartinimo padai nepasirodydavo - modelio nebuvo kaip pasukti (V 08-17).
         Kiekvienam savas display: zoom yra flex, pagalba block, padai grid. */
      const rodyk=id=>{const e=$(id);if(e)e.style.display=
        id==='gl3dZoom'?'flex':(id==='gl3dHelp'?'block':'grid');};
      rodyk('gl3dHelp');rodyk('gl3dPad');rodyk('gl3dRot');rodyk('gl3dZoom');
      /* Priartinimo grupeje pas MUS gyvena ir narvas, pjuvis, vokseliai bei
         detalus vaizdas - experimental2 ju ten neturejo. Visi keturi turi prasme
         tik SUPJAUSTYTIEMS duomenims: sliceryje dar nera ka pjauti nei rodyti
         vokseliais, o „detalus vaizdas" dar ir maisosi su slicerio „minkstinimu"
         (panasi ikona, kitas dalykas). Be to su sesiais mygtukais grupe
         persidengia su irankiu juosta (V 08-17). Paliekam tik priartinima. */
      SLICER_HIDE.forEach(id=>{const e=$(id);if(e)e.style.display='none';});
      // Lango didinimas: sliceryje jis lygiai toks pat naudingas, kaip perziuroje.
      if(typeof setStageGrowUI==='function')setStageGrowUI(true);
      clipTaikyk();                        // medziaga nauja - pjuvis is naujo
      ren.render(scene,cam);
      return true;
    }catch(e){return false;}
  };

  /* Atramos - ATSKIRA medziaga ir spalva, ne to paties tinklelio dalis: V 08-16
     pastebejo, kad be to nesimato, kur baigiasi detale ir prasideda atrama. */
  window.gl3dSupports=positions=>{
    if(!ren)return false;
    if(supMesh){scene.remove(supMesh);supMesh.geometry.dispose();
                supMesh.material.dispose();supMesh=null;}
    if(!positions||!positions.length){if(scene&&cam)ren.render(scene,cam);return false;}
    const geo=new T.BufferGeometry();
    geo.setAttribute('position',new T.BufferAttribute(positions,3));
    geo.computeVertexNormals();
    supMesh=new T.Mesh(geo,new T.MeshStandardMaterial({color:0x8fa8c8,roughness:.75,
      metalness:.05,side:T.DoubleSide}));
    // Tas pats pjuvis kaip modeliui - kitaip slankiklis pjautu tik detale.
    if(mesh&&mesh.material&&mesh.material.clippingPlanes)
      supMesh.material.clippingPlanes=mesh.material.clippingPlanes;
    scene.add(supMesh);
    if(scene&&cam)ren.render(scene,cam);
    return true;
  };

  // Kadruoja modeli KARTU su atramomis (gl3dFrameModel mato tik modeli).
  window.gl3dFrameAll=()=>{
    if(!ren||!scene||!cam)return false;
    const box=new T.Box3();
    for(const m of [mesh,supMesh]) if(m){
      m.geometry.computeBoundingBox();
      box.union(m.geometry.boundingBox);
    }
    if(box.isEmpty())return false;
    const c=new T.Vector3(),s=new T.Vector3();
    box.getCenter(c);box.getSize(s);
    return frameBox(c,s.x/2,s.y/2,s.z/2);
  };

  // Sliceris pakeite tinkleli uz musu nugaros - kitas piesimas privalo persistatyti.
  window.key3dReset=()=>{key3d='';framedFor='';};